#ifndef XGEOM_STATIC_EDITOR_PREVIEW_H
#define XGEOM_STATIC_EDITOR_PREVIEW_H
#pragma once

// The Static Geom editor's 3D preview: the geometry lit with a shadow map (the light's view is rendered first, into its own
// texture), plus wire frame, tangent/binormal/normal lines and a grid. The lit geometry is drawn by xLIONRender (the materials'
// HDR variants, tone mapped by the renderer) into this runtime's render_view; the editor's overlays (grid, wire frame, lines) are
// drawn over the tone-mapped image with the scene's depth. Render opens its own render passes on the window, so it runs before
// the frame's UI is rendered; the panel then shows the view's output texture.
#include "source/Tools/xgpu_imgui_breach.h"
#include "dependencies/xLIONRender/src/xlionrender.h"
#include "source/Tools/xgpu_xcore_bitmap_helpers.h"
#include "dependencies/xeditor_tools/src/xeditor_tools_grid.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_runtime.h"
#include "plugins/xmaterial.plugin/source/xmaterial_runtime.h"
#include "plugins/xgeom_static.plugin/source/Editor/xgeom_static_editor_inspectors.h"

#include <array>
#include <cstdio>

namespace xgeom_static_editor::preview
{
    constexpr static std::uint32_t g_FallbackFragShader[] =
    {
        #include "xgeom_static_preview_fallback_frag.h"
    };
    constexpr static std::uint32_t g_ShadowFragShader[] =
    {
        #include "GeomStaticShadowMapCreation_frag.h"
    };
    constexpr static std::uint32_t g_ShadowVertShader[] =
    {
        #include "GeomStaticShadowMapCreation_vert.h"
    };
    constexpr static std::uint32_t g_WireframeFragShader[] =
    {
        #include "xgeom_static_wireframe_frag.h"
    };
    constexpr static std::uint32_t g_WireframeVertShader[] =
    {
        #include "xgeom_static_wireframe_vert.h"
    };
    constexpr static std::uint32_t g_WireframeGeomShader[] =
    {
        #include "xgeom_static_wireframe_geom.h"
    };
    constexpr static std::uint32_t g_DebugNormalFragShader[] =
    {
        #include "xgeom_static_debug_normal_frag.h"
    };
    constexpr static std::uint32_t g_DebugNormalVertShader[] =
    {
        #include "xgeom_static_debug_normal_vert.h"
    };
    constexpr static std::uint32_t g_DebugNormalGeomShader[] =
    {
        #include "xgeom_static_debug_normal_geom.h"
    };

    struct alignas(256) ubo_debug_normal
    {
        xmath::fmat4 m_L2C;
        xmath::fvec4 m_ScaleFactor;
        xmath::fvec4 m_Color;
    };

    // alignas(256) like the others: a UBO entry must be a multiple of minUniformBufferOffsetAlignment - 64 on NVIDIA, 256 on Mesa's D3D12
    // driver under WSL, where this one (64 bytes) made Init fail, so the preview and every thumbnail never initialized
    struct alignas(256) ubo_shadow_generation
    {
        xmath::fmat4    m_L2C;
    };

    struct alignas(256) ubo_wireframe
    {
        xmath::fmat4  m_L2w;
        xmath::fmat4  m_w2C;
    };

    struct push_const
    {
        std::uint32_t   m_ClusterIndex;         // which cluster the draw is for
    };

    struct runtime
    {
        using geom = xgeom_static::xgpu::geom;

        xgpu::device*               m_pDevice = nullptr;
        bool                        m_bReady  = false;
        render_settings             m_Settings;

        // Frame state RenderShadow leaves for Draw
        xmath::fmat4                m_ShadowL2C;

        xgpu::vertex_descriptor     m_GeomVD, m_ShadowVD;
        xgpu::buffer                m_NormalUBO, m_ShadowUBO, m_WireUBO;
        xgpu::pipeline              m_NormalPipeline, m_ShadowPipeline, m_WirePipeline;
        xgpu::pipeline_instance     m_NormalInstance, m_ShadowInstance, m_WireInstance;
        xgpu::texture               m_DefaultTexture;
        xgpu::shader                m_FallbackFrag;         // HDR shader for a submesh whose material does not resolve

        // The renderer the lit geometry goes through (one instance per runtime: the preview and each thumbnail renderer own theirs)
        xlionrender::instance                   m_Renderer;
        xlionrender::render_view                m_RenderView;
        xlionrender::mesh_id                    m_Mesh;
        std::vector<xlionrender::material_id>   m_Materials;    // one per material slot of the geometry

        // The ground grid and the shadow map this runtime's own shadow-caster pipeline (m_ShadowPipeline
        // above) draws into - shared with every other 3D editor viewport via xeditor_tools, instead of
        // this runtime's own now-removed copy of the grid pipeline/shader/vertex-descriptor/mesh.
        xeditor_tools::grid         m_Grid;

        // The material instances the geometry references, held while m_Materials uses their shaders and textures
        std::vector<xrsc::material_instance_ref>    m_MatRefs;

        static bool Ok(xgpu::device::error* pErr) noexcept
        {
            if (!pErr) return true;
            std::printf("Static Geom preview: %s\n", std::string(xgpu::getErrorMsg(pErr)).c_str());
            return false;
        }

        static xgpu::vertex_descriptor::setup GeomVertexSetup(std::span<xgpu::vertex_descriptor::attribute> Attributes) noexcept
        {
            return { .m_bUseStreaming = true, .m_Topology = xgpu::vertex_descriptor::topology::TRIANGLE_LIST, .m_VertexSize = 0, .m_Attributes = Attributes };
        }

        template<std::size_t N>
        static xgpu::shader::setup ShaderSetup(xgpu::shader::type::bit Type, const std::uint32_t (&Code)[N]) noexcept
        {
            return { .m_Type = Type, .m_Sharer = xgpu::shader::setup::raw_data{ std::span{ (std::int32_t*)Code, N } } };
        }

        bool Init(xgpu::device& Device) noexcept
        {
            if (m_bReady) return true;
            m_pDevice = &Device;
            m_Settings.clear();
            if (!m_Grid.Init(Device, true)) return false;   // always shadow-enabled here - both the interactive preview and the thumbnail need a real shadow

            if (!Ok(m_Renderer.Init(Device))) return false;
            m_RenderView.m_Background = { 0.45f, 0.45f, 0.45f, 1.0f };     // the grey the preview and the thumbnails always had
            if (!Ok(Device.Create(m_FallbackFrag, ShaderSetup(xgpu::shader::type::bit::FRAGMENT, g_FallbackFragShader)))) return false;

            auto UBO = [&](xgpu::buffer& B, int Size, int Count) { return Ok(Device.Create(B, { .m_Type = xgpu::buffer::type::UNIFORM, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = Size, .m_EntryCount = Count })); };
            if (!UBO(m_NormalUBO, sizeof(ubo_debug_normal),      100) || !UBO(m_ShadowUBO,sizeof(ubo_shadow_generation),100)
             || !UBO(m_WireUBO,   sizeof(ubo_wireframe),         100)) return false;

            // Vertex layout for the compiled geometry (position stream + extras stream), position only for the shadow/wire passes
            {
                auto Attributes = std::array
                { xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(xgeom_static::geom::vertex, m_XPos),            .m_Format = xgpu::vertex_descriptor::format::SINT16_4D, .m_iStream = 0 }
                , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(xgeom_static::geom::vertex_extras, m_UV),        .m_Format = xgpu::vertex_descriptor::format::UINT16_2D, .m_iStream = 1 }
                , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(xgeom_static::geom::vertex_extras, m_OctNormal), .m_Format = xgpu::vertex_descriptor::format::UINT8_4D,  .m_iStream = 1 }
                };
                if (!Ok(Device.Create(m_GeomVD, GeomVertexSetup(Attributes)))) return false;
            }
            {
                auto Attributes = std::array{ xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(xgeom_static::geom::vertex, m_XPos), .m_Format = xgpu::vertex_descriptor::format::SINT16_4D } };
                if (!Ok(Device.Create(m_ShadowVD, xgpu::vertex_descriptor::setup{ .m_Topology = xgpu::vertex_descriptor::topology::TRIANGLE_LIST, .m_VertexSize = sizeof(xgeom_static::geom::vertex), .m_Attributes = Attributes }))) return false;
            }

            // The uniform bindings every geometry pipeline shares: per-draw mesh block, and the cluster table
            auto MeshBind    = xgpu::pipeline::uniform_binds{ .m_BindIndex = 0, .m_Usage = { .m_bVertex = true },   .m_Type = xgpu::pipeline::uniform_binds::type::UBO_DYNAMIC };
            auto ClusterBind = xgpu::pipeline::uniform_binds{ .m_BindIndex = 0, .m_Usage = xgpu::shader::type{ xgpu::shader::type::bit::VERTEX }, .m_Type = xgpu::pipeline::uniform_binds::type::SSBO_STATIC };

            // Tangent / binormal / normal lines
            {
                xgpu::shader Frag, Vert, Geom;
                if (!Ok(Device.Create(Frag, ShaderSetup(xgpu::shader::type::bit::FRAGMENT, g_DebugNormalFragShader)))) return false;
                if (!Ok(Device.Create(Vert, ShaderSetup(xgpu::shader::type::bit::VERTEX,   g_DebugNormalVertShader)))) return false;
                if (!Ok(Device.Create(Geom, ShaderSetup(xgpu::shader::type::bit::GEOMETRY, g_DebugNormalGeomShader)))) return false;

                auto Shaders = std::array<const xgpu::shader*, 3>{ &Frag, &Vert, &Geom };
                auto Binds   = std::array{ MeshBind, ClusterBind };
                if (!Ok(Device.Create(m_NormalPipeline, xgpu::pipeline::setup{ .m_VertexDescriptor = m_GeomVD, .m_Shaders = Shaders, .m_PushConstantsSize = sizeof(push_const), .m_UniformBinds = Binds, .m_Blend = xgpu::pipeline::blend::getAlphaOriginal() }))) return false;
                if (!Ok(Device.Create(m_NormalInstance, { .m_PipeLine = m_NormalPipeline }))) return false;
            }

            // The light's view of the geometry
            {
                xgpu::shader Frag, Vert;
                if (!Ok(Device.Create(Frag, ShaderSetup(xgpu::shader::type::bit::FRAGMENT, g_ShadowFragShader)))) return false;
                if (!Ok(Device.Create(Vert, ShaderSetup(xgpu::shader::type::bit::VERTEX,   g_ShadowVertShader)))) return false;

                auto Shaders = std::array<const xgpu::shader*, 2>{ &Vert, &Frag };
                auto Binds   = std::array{ MeshBind, ClusterBind };
                if (!Ok(Device.Create(m_ShadowPipeline, xgpu::pipeline::setup
                    { .m_VertexDescriptor  = m_ShadowVD
                    , .m_Shaders           = Shaders
                    , .m_PushConstantsSize = sizeof(push_const)
                    , .m_UniformBinds      = Binds
                    , .m_Primitive         = {}
                    , .m_DepthStencil      = { .m_DepthBiasConstantFactor = 2.00f     // depth bias and slope avoid shadowing artifacts
                                             , .m_DepthBiasSlopeFactor    = 4.0f
                                             , .m_bDepthBiasEnable        = true
                                             , .m_bDepthClampEnable       = true      // no near plane clipping
                                             }
                    }))) return false;
                if (!Ok(Device.Create(m_ShadowInstance, { .m_PipeLine = m_ShadowPipeline }))) return false;
            }

            // Wire frame
            {
                xgpu::shader Frag, Vert, Geom;
                if (!Ok(Device.Create(Frag, ShaderSetup(xgpu::shader::type::bit::FRAGMENT, g_WireframeFragShader)))) return false;
                if (!Ok(Device.Create(Vert, ShaderSetup(xgpu::shader::type::bit::VERTEX,   g_WireframeVertShader)))) return false;
                if (!Ok(Device.Create(Geom, ShaderSetup(xgpu::shader::type::bit::GEOMETRY, g_WireframeGeomShader)))) return false;

                auto Shaders = std::array<const xgpu::shader*, 3>{ &Frag, &Geom, &Vert };
                auto Binds   = std::array{ MeshBind, ClusterBind };
                if (!Ok(Device.Create(m_WirePipeline, xgpu::pipeline::setup{ .m_VertexDescriptor = m_ShadowVD, .m_Shaders = Shaders, .m_PushConstantsSize = sizeof(push_const), .m_UniformBinds = Binds, .m_Blend = xgpu::pipeline::blend::getAlphaOriginal() }))) return false;
                if (!Ok(Device.Create(m_WireInstance, { .m_PipeLine = m_WirePipeline }))) return false;
            }

            // What geometry without a material is drawn with
            if (auto* pErr = xgpu::tools::bitmap::Create(m_DefaultTexture, Device, xbitmap::getDefaultBitmap()); pErr) return false;

            m_bReady = true;
            return true;
        }

        // The renderer's mesh and materials for the geometry (called after every (re)load of the compiled resource). The mesh
        // references the geometry's own GPU buffers; each material slot gets the material's HDR shader with the shadow map in
        // slot 0 and the material instance's textures after it, or the fallback shader when anything does not resolve.
        void RebuildMaterials(geom& Geom) noexcept
        {
            ReleaseMaterials();
            m_Renderer.DestroyMesh(m_Mesh);
            (void)Ok(m_Renderer.CreateStaticGeom(m_Mesh, xlionrender::MakeStaticGeom(Geom)));

            const auto Materials = Geom.getDefaultMaterialInstances();
            m_MatRefs.resize(Materials.size());
            m_Materials.resize(Materials.size());

            for (auto& MI : Materials)
            {
                const auto Index = static_cast<int>(&MI - Materials.data());
                xresource::g_Mgr.CloneRef(m_MatRefs[Index], MI);

                // An empty ref, or one that fails to resolve at any step below (the material instance, its material, one of its
                // textures), draws with the fallback - the geometry should still render with something instead of vanishing.
                const auto UseDefaultMaterial = [&]
                {
                    const auto Textures = std::array<xgpu::texture*, 4>{ &m_Grid.m_ShadowMap, &m_DefaultTexture, &m_DefaultTexture, &m_DefaultTexture };
                    (void)Ok(m_Renderer.CreateMaterial(m_Materials[Index], m_FallbackFrag, Textures));
                };

                if (MI.empty()) { UseDefaultMaterial(); continue; }

                xmaterial_instance::rt* pMI = xresource::g_Mgr.getResource(m_MatRefs[Index]);
                if (!pMI) { UseDefaultMaterial(); continue; }
                auto* pMat = xresource::g_Mgr.getResource(pMI->m_MaterialRef);
                if (!pMat) { UseDefaultMaterial(); continue; }

                // Slot 0 of every material is the shadow map; the others are the material instance's textures
                std::vector<xgpu::texture*> Textures;
                Textures.reserve(pMI->m_nTexturesList);
                bool bTextures = true;
                for (auto& E : pMI->getTextures())
                {
                    if (&E == pMI->getTextures().data()) { Textures.push_back(&m_Grid.m_ShadowMap); continue; }
                    auto* pTexture = xresource::g_Mgr.getResource(E.m_TexureRef);
                    if (!pTexture) { bTextures = false; break; }
                    Textures.push_back(pTexture);
                }
                if (!bTextures || Textures.empty()) { UseDefaultMaterial(); continue; }

                if (!Ok(m_Renderer.CreateMaterial(m_Materials[Index], pMat->getShaderHDR(), Textures))) UseDefaultMaterial();
            }
        }

        void ReleaseMaterials() noexcept
        {
            for (auto& E : m_Materials) m_Renderer.DestroyMaterial(E);
            for (auto& E : m_MatRefs) xresource::g_Mgr.ReleaseRef(E);
            m_Materials.clear();
            m_MatRefs.clear();
        }

        ~runtime() noexcept
        {
            ReleaseMaterials();
            if (m_bReady) m_Renderer.DestroyView(m_RenderView);
            xeditor::DestroyGpu( m_pDevice, m_NormalInstance, m_ShadowInstance, m_WireInstance
                               , m_NormalPipeline, m_ShadowPipeline, m_WirePipeline
                               , m_DefaultTexture );
            m_Grid.Release();
        }

        // Right drag turns the camera, middle drag pans, the wheel zooms; the light-follows-camera key is an action (GeomStatic/Preview/LightFollowsCamera).
        // Call right after the preview canvas item was submitted.
        void HandleInput() noexcept
        {
            if (!ImGui::IsItemHovered() && !ImGui::IsItemActive()) return;
            auto& io = ImGui::GetIO();
            auto& S  = m_Settings;

            if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
                S.m_Angles.m_Pitch.m_Value -= 0.01f * io.MouseDelta.y;
                S.m_Angles.m_Yaw.m_Value   -= 0.01f * io.MouseDelta.x;
            }

            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            {
                S.m_CameraTarget += S.m_View.getWorldYVector() * (0.005f * io.MouseDelta.y);
                S.m_CameraTarget += S.m_View.getWorldXVector() * (0.005f * io.MouseDelta.x);
            }

            if (S.m_Distance != -1)
            {
                S.m_Distance += S.m_Distance * -0.2f * io.MouseWheel;
                if (S.m_Distance < 0.5f)
                {
                    S.m_CameraTarget += S.m_View.getWorldZVector() * (0.5f * (0.5f - S.m_Distance));
                    S.m_Distance = 0.5f;
                }
            }

            // F (light follows the camera) is the GeomStatic/Preview/LightFollowsCamera action (xgeom_static_editor.h).

            // WASD/QE fly, same extension as xeditor_tools::camera - kept as a local copy rather than
            // inheriting that class here since render_settings is xproperty-reflected with its own field
            // names (m_CameraTarget, no m_bReframe) shared with the thumbnail renderer; unifying that is
            // its own separate pass.
            if (!io.WantTextInput)
            {
                constexpr float FlySpeed = 1.0f;   // matches xeditor_tools::camera's own default
                const float Speed = FlySpeed * std::max(S.m_Distance, 0.1f) * io.DeltaTime;
                xmath::fvec3 Move(0, 0, 0);
                if (ImGui::IsKeyDown(ImGuiKey_W)) Move += S.m_View.getWorldZVector();
                if (ImGui::IsKeyDown(ImGuiKey_S)) Move -= S.m_View.getWorldZVector();
                if (ImGui::IsKeyDown(ImGuiKey_A)) Move += S.m_View.getWorldXVector();
                if (ImGui::IsKeyDown(ImGuiKey_D)) Move -= S.m_View.getWorldXVector();
                if (ImGui::IsKeyDown(ImGuiKey_E)) Move += S.m_View.getWorldYVector();
                if (ImGui::IsKeyDown(ImGuiKey_Q)) Move -= S.m_View.getWorldYVector();
                if (Move.Length() > 0.0f) S.m_CameraTarget += Move.Normalize() * Speed;
            }
        }

        // The light's view: the geometry into the shadow map. Opens its own render pass on the window, so it must run before the frame's
        // UI is rendered.
        void RenderShadow(xgpu::window& Window, geom& Geom, float ViewW, float ViewH) noexcept
        {
            if (!m_bReady || ViewW <= 1.f || ViewH <= 1.f) return;
            auto& S = m_Settings;

            S.m_LightingView.setViewport({ 0, 0, m_Grid.m_ShadowMap.getTextureDimensions()[0], m_Grid.m_ShadowMap.getTextureDimensions()[1] });
            S.m_View.setViewport({ 0, 0, static_cast<int>(ViewW), static_cast<int>(ViewH) });

            if (S.m_LightFollowsCamera)
            {
                S.m_LightDirection = -S.m_View.getPosition();
                S.m_LightPosition  = S.m_View.getPosition();
                S.m_LightDirection.NormalizeSafeCopy();
            }

            // Look at the object from the light, far enough to keep all of it in view
            {
                const float VerticalFov = S.m_LightingView.getFov().m_Value;
                const float Aspect      = S.m_LightingView.getAspect();
                const float Radius      = Geom.m_BBox.getRadius();
                const float HFov        = 2.0f * std::atan(Aspect * std::tan(VerticalFov / 2.0f));
                const float Distance    = Radius / std::tan(std::min(VerticalFov, HFov) / 2.0f);

                auto L = -S.m_LightDirection;
                S.m_LightingView.LookAt(Distance, xmath::radian3(L.Pitch(), L.Yaw(), 0_xdeg), Geom.m_BBox.getCenter());
                if (S.m_Distance == -1) S.m_LightPosition = S.m_LightingView.getPosition();
            }

            auto CmdBuffer = Window.StartRenderPass(m_Grid.m_ShadowPass);

            std::array StaticUBO{ &Geom.ClusterBuffer() };
            CmdBuffer.setPipelineInstance(m_ShadowInstance, StaticUBO);

            {
                auto& Entry = m_ShadowUBO.allocEntry<ubo_shadow_generation>();
                Entry.m_L2C = S.m_LightingView.getW2C();
                m_ShadowL2C = Entry.m_L2C;
            }

            CmdBuffer.setBuffer(Geom.IndexBuffer());
            CmdBuffer.setBuffer(Geom.VertexBuffer());
            CmdBuffer.setDynamicUBO(m_ShadowUBO, 0);
            DrawClusters(CmdBuffer, Geom);
        }

        // Every cluster of every LOD, each with its own index into the cluster table
        static void DrawClusters(xgpu::cmd_buffer& CmdBuffer, geom& Geom) noexcept
        {
            for (auto& M : Geom.getMeshes())
                for (auto& L : Geom.getLODs().subspan(M.m_iLOD, M.m_nLODs))
                    for (auto& Sub : Geom.getSubmeshes().subspan(L.m_iSubmesh, L.m_nSubmesh))
                    {
                        push_const PushConstant{ Sub.m_iCluster };
                        for (auto& C : Geom.getClusters().subspan(Sub.m_iCluster, Sub.m_nCluster))
                        {
                            CmdBuffer.setPushConstants(PushConstant);
                            CmdBuffer.Draw(C.m_nIndices, C.m_iIndex, C.m_iVertex);
                            PushConstant.m_ClusterIndex++;
                        }
                    }
        }

        // The geometry through xLIONRender into m_RenderView (ViewW x ViewH): the shadow map first, then the lit geometry, then the
        // editor's overlays over the tone-mapped image. Opens render passes on the window: call before the frame's UI is rendered,
        // then show m_RenderView.m_Output with m_Renderer.Blit (display encoded already, not ImGui::Image).
        void Render(xgpu::window& Window, geom& Geom, float ViewW, float ViewH) noexcept
        {
            if (!m_bReady || ViewW <= 1.f || ViewH <= 1.f || !m_Mesh.isValid()) return;
            auto& S = m_Settings;

            const int W = static_cast<int>(ViewW), H = static_cast<int>(ViewH);
            if (m_RenderView.m_Width != W || m_RenderView.m_Height != H)
                if (!Ok(m_Renderer.CreateView(m_RenderView, { .m_Width = W, .m_Height = H }))) return;

            RenderShadow(Window, Geom, ViewW, ViewH);

            static const xmath::fmat4 ClipToTexture = []
            {
                xmath::fmat4 M;
                M.setupSRT({ 0.5f, 0.5f, 1.0f }, { 0_xdeg }, { 0.5f, 0.5f, 0.0f });
                return M;
            }();

            if (S.m_Distance == -1)
            {
                // Fit the geometry in the view
                const float VerticalFov = S.m_View.getFov().m_Value;
                const float Aspect      = S.m_View.getAspect();
                const float Radius      = Geom.m_BBox.getRadius();
                const float HFov        = 2.0f * std::atan(Aspect * std::tan(VerticalFov / 2.0f));
                S.m_Distance     = Radius / std::tan(std::min(VerticalFov, HFov) / 2.0f);
                S.m_CameraTarget = Geom.m_BBox.getCenter();
            }

            S.m_View.LookAt(S.m_Distance, S.m_Angles, S.m_CameraTarget);

            // The same light the preview always had: a warm point light, softened by its area, with a soft fill
            const float Radius = Geom.m_BBox.getRadius();
            xlionrender::scene Scene;
            Scene.m_Draws.push_back({ .m_Mesh = m_Mesh, .m_Materials = m_Materials });
            Scene.m_Light     = { .m_Position = S.m_LightPosition, .m_Radius = Radius * 5, .m_AreaRadius = Radius * 0.5f, .m_Temperature = 6500, .m_Intensity = 4 };
            Scene.m_Ambient   = { 0.7f, 0.7f, 0.7f };
            Scene.m_W2ShadowT = ClipToTexture * m_ShadowL2C;

            m_Renderer.Render(Window, Scene, m_RenderView, S.m_View, [&](xgpu::cmd_buffer& CmdBuffer) { DrawOverlays(CmdBuffer, Geom); });
        }

        // The wire frame, the grid and the tangent/binormal/normal lines, in display colors over the tone-mapped image
        void DrawOverlays(xgpu::cmd_buffer& CmdBuffer, geom& Geom) noexcept
        {
            auto& S = m_Settings;

            // Everything is drawn relative to the camera
            const auto L2w = xmath::fmat4::fromTranslation(-S.m_View.getPosition());
            const auto w2C = S.m_View.getW2C() * xmath::fmat4::fromTranslation(S.m_View.getPosition());

            if (S.m_bWireFrame)
            {
                std::array StaticUBO{ &Geom.ClusterBuffer() };
                CmdBuffer.setPipelineInstance(m_WireInstance, StaticUBO);

                auto& MeshUBO = m_WireUBO.allocEntry<ubo_wireframe>();
                MeshUBO.m_w2C = w2C;
                MeshUBO.m_L2w = L2w;
                CmdBuffer.setDynamicUBO(m_WireUBO, 0);
                DrawClusters(CmdBuffer, Geom);
            }

            // The grid sits at the bottom of the geometry
            {
                S.m_GridYMin = S.m_bGridToYMin ? Geom.m_BBox.m_Min.m_Y : 0;
                m_Grid.Draw(CmdBuffer, S.m_View.getW2C(), S.m_View.getPosition(), m_ShadowL2C, S.m_GridYMin);
                S.m_GridYMin = Geom.m_BBox.m_Min.m_Y;       // the property shows where the geometry ends, whatever the grid does
            }

            // Tangent, binormal and normal lines
            {
                static constexpr auto Colors = std::array{ xmath::fvec4{1,0,0,1}, xmath::fvec4{0,1,0,1}, xmath::fvec4{0,0,1,1} };
                static constexpr auto Axis   = std::array{ xmath::fvec4{1,0,0,0}, xmath::fvec4{0,1,0,1}, xmath::fvec4{0,0,1,1} };
                const auto List = std::array{ S.m_bTangents, S.m_bBinormals, S.m_bNormals };
                for (size_t i = 0; i < List.size(); ++i)
                {
                    if (!List[i]) continue;

                    std::array StaticUBO{ &Geom.ClusterBuffer() };
                    CmdBuffer.setPipelineInstance(m_NormalInstance, StaticUBO);

                    auto& MeshUBO         = m_NormalUBO.allocEntry<ubo_debug_normal>();
                    MeshUBO.m_L2C         = w2C * L2w;
                    MeshUBO.m_ScaleFactor = Axis[i];
                    MeshUBO.m_ScaleFactor.m_W = Geom.m_BBox.getRadius();
                    MeshUBO.m_Color       = Colors[i];

                    CmdBuffer.setDynamicUBO(m_NormalUBO, 0);
                    CmdBuffer.setStreamingBuffers({ &Geom.IndexBuffer(), 3 });
                    DrawClusters(CmdBuffer, Geom);
                }
            }
        }
    };
}

#endif // XGEOM_STATIC_EDITOR_PREVIEW_H
