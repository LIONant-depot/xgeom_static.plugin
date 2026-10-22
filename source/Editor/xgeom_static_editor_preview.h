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
#include "dependencies/xeditor_tools/src/xeditor_tools_camera.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_runtime.h"
#include "plugins/xmaterial.plugin/source/xmaterial_runtime.h"
#include "plugins/xmaterial.plugin/source/xmaterial_xlionrender.h"
#include "plugins/xgeom_static.plugin/source/Editor/xgeom_static_editor_inspectors.h"
#include "source/Tools/Editor/xeditor_scale_reference.h"

#include <array>
#include <cstdio>

namespace xgeom_static_editor::preview
{
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

        // The renderer the lit geometry goes through (one instance per runtime: the preview and each thumbnail renderer own theirs)
        xlionrender::instance                   m_Renderer;
        xlionrender::render_view                m_RenderView;
        xlionrender::mesh_id                    m_Mesh;
        const geom*                             m_pMeshGeom = nullptr;  // the geometry m_Mesh was made from (Render rebuilds for another one)
        bool                                    m_bGIReady = false;     // the mesh has its surfels (the first GI view after a load)
        xmath::fmat4                            m_PendingL2W = xmath::fmat4::fromIdentity();   // the editor: the mesh at the Scale/Translation being edited, before it is compiled (xeditor_scale_reference.h)
        std::vector<xlionrender::material_id>   m_Materials;    // one per material slot of the geometry

        // The ground grid and the shadow map this runtime's own shadow-caster pipeline (m_ShadowPipeline
        // above) draws into - shared with every other 3D editor viewport via xeditor_tools, instead of
        // this runtime's own now-removed copy of the grid pipeline/shader/vertex-descriptor/mesh.
        xeditor_tools::grid         m_Grid;

        // The material instances the geometry references, held while m_Materials uses their shaders and textures
        std::vector<xmaterial_instance::held>       m_MatRefs;      // what m_Materials were made of, held (their GPU objects point at it)

        // The environment cubemap (render_settings::m_Environment), held while the renderer lights with it
        xrsc::texture_ref                           m_EnvRef{};
        std::uint64_t                               m_EnvInstance = 0;

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
                // The overlay pass depth-tests against the renderer's scene: the wire lies on that surface, so it is pulled toward the camera
                xgpu::pipeline::setup Setup{ .m_VertexDescriptor = m_ShadowVD, .m_Shaders = Shaders, .m_PushConstantsSize = sizeof(push_const), .m_UniformBinds = Binds, .m_Blend = xgpu::pipeline::blend::getAlphaOriginal() };
                Setup.m_DepthStencil.m_DepthBiasConstantFactor = -4.0f;
                Setup.m_DepthStencil.m_DepthBiasSlopeFactor    = -2.0f;
                Setup.m_DepthStencil.m_bDepthBiasEnable        = true;
                Setup.m_DepthStencil.m_bDepthWriteEnable       = false;
                if (!Ok(Device.Create(m_WirePipeline, Setup))) return false;
                if (!Ok(Device.Create(m_WireInstance, { .m_PipeLine = m_WirePipeline }))) return false;
            }

            // What geometry without a material is drawn with
            if (auto* pErr = xgpu::tools::bitmap::Create(m_DefaultTexture, Device, xbitmap::getDefaultBitmap()); pErr) return false;

            m_bReady = true;
            return true;
        }

        // The renderer's mesh and materials for the geometry (called after every (re)load of the compiled resource). The mesh
        // references the geometry's own GPU buffers; each material slot gets the material's HDR shader with the shadow map in
        // slot 0 and the material instance's textures after it, or the renderer's default material when anything does not resolve.
        // Something the renderer's materials were made from compiled again (xresource::mgr::MarkUpdated: a material instance, its
        // material - a shader it includes changed - or one of its textures): Render makes them again (getResource moves the references)
        bool MaterialsStale() const noexcept
        {
            for (const auto& H : m_MatRefs) if (H.isStale()) return true;
            return false;
        }

        void RebuildMaterials(geom& Geom) noexcept
        {
            ReleaseMaterials();
            m_Renderer.DestroyMesh(m_Mesh);
            (void)Ok(m_Renderer.CreateClusterGeom(m_Mesh, xlionrender::MakeClusterGeom(Geom)));
            m_pMeshGeom = &Geom;
            m_bGIReady = false;

            const auto Materials = Geom.getDefaultMaterialInstances();
            m_MatRefs.resize(Materials.size());
            m_Materials.resize(Materials.size());

            for (auto& MI : Materials)
            {
                const auto Index = static_cast<int>(&MI - Materials.data());

                // An empty ref, or one that fails to resolve at any step below (the material instance, its material, one of its
                // textures), draws with the fallback - the geometry should still render with something instead of vanishing.
                const auto UseDefaultMaterial = [&]
                {
                    const auto Textures = std::array<xgpu::texture*, 4>{ &m_Grid.m_ShadowMap, &m_DefaultTexture, &m_DefaultTexture, &m_DefaultTexture };
                    (void)Ok(m_Renderer.CreateDefaultMaterial(m_Materials[Index], Textures));
                };

                if (MI.empty()) { UseDefaultMaterial(); continue; }

                auto& Held = m_MatRefs[Index];
                xmaterial_instance::rt* pMI = Held.Take(MI);
                if (!pMI) { UseDefaultMaterial(); continue; }
                auto* pMat = xresource::g_Mgr.getResource(Held.m_Material);
                if (!pMat) { UseDefaultMaterial(); continue; }

                // Slot 0 of every material is the shadow map; the others are the material instance's textures
                std::vector<xgpu::texture*> Textures;
                if (pMat->m_Flags.m_bSamplersFrom1) Textures.push_back(&m_Grid.m_ShadowMap);    // a graph material: its textures are samplers 1.. (same rule as xlionrender's MaterialOf)
                Textures.reserve(pMI->m_nTexturesList);
                bool bTextures = true;
                for (auto& E : pMI->getTextures())
                {
                    if (!pMat->m_Flags.m_bSamplersFrom1 && &E == pMI->getTextures().data()) { Textures.push_back(&m_Grid.m_ShadowMap); continue; }
                    auto* pTexture = xresource::g_Mgr.getResource(Held.m_Textures[&E - pMI->getTextures().data()]);
                    if (!pTexture) { bTextures = false; break; }
                    Textures.push_back(pTexture);
                }
                if (!bTextures || Textures.empty()) { UseDefaultMaterial(); continue; }

                if (!Ok(m_Renderer.CreateMaterial(m_Materials[Index], pMat->getShaderHDR(), Textures, xmaterial::RenderState(pMat->m_Render, pMI->m_DoubleSided), { pMat->m_pDefaultBlock, pMat->m_BlockBytes / 4u }))) UseDefaultMaterial();
            }
        }

        void ReleaseMaterials() noexcept
        {
            for (auto& E : m_Materials) m_Renderer.DestroyMaterial(E);
            for (auto& E : m_MatRefs) E.Release();
            m_Materials.clear();
            m_MatRefs.clear();
        }

        ~runtime() noexcept
        {
            ReleaseMaterials();
            xresource::g_Mgr.ReleaseRef(m_EnvRef);
            if (m_bReady) m_Renderer.DestroyView(m_RenderView);
            xeditor::DestroyGpu( m_pDevice, m_NormalInstance, m_ShadowInstance, m_WireInstance
                               , m_NormalPipeline, m_ShadowPipeline, m_WirePipeline
                               , m_DefaultTexture );
            m_Grid.Release();
        }

        // The camera is xeditor_tools' (xeditor_tools_camera.h): Orbit or Fly, F switches. The light-follows-camera key is an action (GeomStatic/Preview/LightFollowsCamera).
        // Call right after the preview canvas item was submitted.
        void HandleInput() noexcept
        {
            auto& S = m_Settings;
            const xeditor_tools::camera_rig Rig{ S.m_Camera, S.m_View, S.m_Angles, S.m_Distance, S.m_CameraTarget };
            xeditor_tools::HandleCameraInput(Rig);
        }

        // F in Fly frames the subject (the center of its bounding box) and goes to Orbit around it: the next Render fits the geometry again (the angles stay).
        // Call each frame; the toolbar button can raise it too.
        void ApplyCameraFrame() noexcept
        {
            if (!m_Settings.m_Camera.m_bFrame) return;
            m_Settings.m_Camera.m_bFrame = false;
            m_Settings.m_Distance = -1;
        }

        // The target / speed overlay of the camera: call it after the render callback of the canvas was added (it draws over the scene), still right after the canvas item
        void DrawOverlay() noexcept
        {
            auto& S = m_Settings;
            xeditor_tools::DrawCameraOverlay({ S.m_Camera, S.m_View, S.m_Angles, S.m_Distance, S.m_CameraTarget });
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
                Entry.m_L2C = S.m_LightingView.getW2C() * m_PendingL2W;
                m_ShadowL2C = S.m_LightingView.getW2C();
            }

            CmdBuffer.setBuffer(Geom.IndexBuffer());
            CmdBuffer.setBuffer(Geom.VertexBuffer());
            CmdBuffer.setDynamicUBO(m_ShadowUBO, 0);
            if (!m_Renderer.m_bBoxesOnly) DrawClusters(CmdBuffer, Geom, &m_Renderer, true);    // the toolbar's Boxes: no caster (the map cleared); its Budget: not the boxes
        }

        // Every cluster of every LOD, each with its own index into the cluster table; but those pRenderer's last scene pass left out (the toolbar's Boxes and Budget: drawn
        // as their boxes, or outside the view). bCasters (the shadow map): only those drawn as their boxes are left out (what is outside the view still casts into it)
        static void DrawClusters(xgpu::cmd_buffer& CmdBuffer, geom& Geom, const xlionrender::instance* pRenderer = nullptr, bool bCasters = false) noexcept
        {
            for (auto& M : Geom.getMeshes())
                for (auto& L : Geom.getLODs().subspan(M.m_iLOD, M.m_nLODs))
                    for (auto& Sub : Geom.getSubmeshes().subspan(L.m_iSubmesh, L.m_nSubmesh))
                    {
                        push_const PushConstant{ Sub.m_iCluster };
                        for (auto& C : Geom.getClusters().subspan(Sub.m_iCluster, Sub.m_nCluster))
                        {
                            if (!pRenderer || !(bCasters ? pRenderer->isClusterBoxed(0, PushConstant.m_ClusterIndex) : pRenderer->isClusterSkipped(0, PushConstant.m_ClusterIndex)))  // the preview's one draw
                            {
                                CmdBuffer.setPushConstants(PushConstant);
                                CmdBuffer.Draw(C.m_nIndices, C.m_iIndex, C.m_iVertex);
                            }
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
            // The geometry compiled again (getResource moved the caller's reference and the old data is gone - the mesh was made from its
            // buffers), or a material did
            if (&Geom != m_pMeshGeom || MaterialsStale()) RebuildMaterials(Geom);
            auto& S = m_Settings;

            const int W = static_cast<int>(ViewW), H = static_cast<int>(ViewH);
            if (m_RenderView.m_Width != W || m_RenderView.m_Height != H)
                if (!Ok(m_Renderer.CreateView(m_RenderView, { .m_Width = W, .m_Height = H }))) return;

            RenderShadow(Window, Geom, ViewW, ViewH);
            S.m_Camera.m_Subject  = Geom.m_BBox.getCenter();      // the FOV of the Fly camera keeps the geometry the same size
            S.m_Camera.m_bSubject = true;
            S.m_Camera.m_SubjectRadius = Geom.m_BBox.getRadius();

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

            // A warm directional key light along the preview's light direction (the renderer's shadow clipmap shadows it; its level-0 tiles
            // are sized to the geometry), with a soft fill
            const float Radius = Geom.m_BBox.getRadius();
            xlionrender::scene Scene;
            Scene.m_Draws.push_back({ .m_Mesh = m_Mesh, .m_L2W = m_PendingL2W, .m_Materials = m_Materials, .m_Instance = 1 });

            // The GI views: the surfels the compiler baked (the first time one is asked for after a load), solved every frame
            if (S.m_GIView != render_settings::gi_view::OFF)
            {
                if (!m_bGIReady)
                {
                    m_bGIReady = true;
                    xlionrender::gi::surfel_mesh Surfels;
                    xlionrender::gi::FromXGeomStatic(Surfels, Geom);
                    if (xlionrender::gi::SurfelsFromXGeomStatic(Surfels, Geom) && Surfels.m_Levels[0].size() < 0xFFFF) (void)m_Renderer.SetMeshGI(m_Mesh, std::move(Surfels));
                }
                using dbg = xlionrender::gi_debug;
                Scene.m_GI = { .m_bEnable = true, .m_RayBudget = 1 << 16
                             , .m_Debug = S.m_GIView == render_settings::gi_view::SURFELS       ? dbg::SURFELS
                                        : S.m_GIView == render_settings::gi_view::LIGHT         ? dbg::LIGHT
                                        : S.m_GIView == render_settings::gi_view::SURFACE_COLOR ? dbg::SURFACE_COLOR
                                        : S.m_GIView == render_settings::gi_view::COVERAGE      ? dbg::COVERAGE : dbg::NONE };
            }
            Scene.m_GI.m_Saturation = S.m_GISaturation;
            auto* pHost = xeditor::host::current();                                 // what the foreground leaves of the host's shared GI allowance
            if (pHost) Scene.m_GI.m_TraceAllowanceMs = pHost->m_GIAllowance.Grant(false);
            Scene.m_Sun     = { .m_Direction = S.m_LightDirection, .m_Temperature = 6500, .m_Intensity = 4, .m_ShadowTileSize = std::max(Radius * 0.5f, 0.001f) };
            Scene.m_Ambient = { 0.7f, 0.7f, 0.7f };

            // The environment cubemap lights instead of the flat fill (a texture that is not a cubemap: the flat fill)
            if (S.m_Environment.m_Instance.m_Value != m_EnvInstance)
            {
                xresource::g_Mgr.ReleaseRef(m_EnvRef);
                m_EnvRef      = S.m_Environment;
                m_EnvInstance = S.m_Environment.m_Instance.m_Value;
            }
            if (const xgpu::texture* pEnv = m_EnvRef.empty() ? nullptr : xresource::g_Mgr.getResource(m_EnvRef); pEnv && pEnv->isCubemap())
                Scene.m_Sky = { .m_Intensity = 1, .m_bVisible = true, .m_pCubemap = pEnv };

            m_Renderer.Render(Window, Scene, m_RenderView, S.m_View, [&](xgpu::cmd_buffer& CmdBuffer) { DrawOverlays(CmdBuffer, Geom); });
            if (pHost && Scene.m_GI.m_bEnable) pHost->m_GIAllowance.Used(float(m_Renderer.getGIStats().m_UpdateGIMs), false);
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
                MeshUBO.m_L2w = L2w * m_PendingL2W;
                CmdBuffer.setDynamicUBO(m_WireUBO, 0);
                CmdBuffer.setBuffer(Geom.IndexBuffer());           // (the overlay pass starts with the renderer's buffers bound)
                CmdBuffer.setBuffer(Geom.VertexBuffer());
                DrawClusters(CmdBuffer, Geom, &m_Renderer);
            }

            // The grid sits at the bottom of the geometry
            {
                xmath::fvec3 BoxMin = Geom.m_BBox.m_Min, BoxMax = Geom.m_BBox.m_Max;
                xeditor::scale_reference::TransformBox(m_PendingL2W, BoxMin, BoxMax);     // the mesh as drawn (the Scale being edited)
                S.m_GridYMin = S.m_bGridToYMin ? BoxMin.m_Y : 0;
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
                    MeshUBO.m_L2C         = w2C * L2w * m_PendingL2W;
                    MeshUBO.m_ScaleFactor = Axis[i];
                    MeshUBO.m_ScaleFactor.m_W = Geom.m_BBox.getRadius();
                    MeshUBO.m_Color       = Colors[i];

                    CmdBuffer.setDynamicUBO(m_NormalUBO, 0);
                    CmdBuffer.setStreamingBuffers({ &Geom.IndexBuffer(), 3 });
                    DrawClusters(CmdBuffer, Geom, &m_Renderer);
                }
            }
        }
    };
}

#endif // XGEOM_STATIC_EDITOR_PREVIEW_H
