#ifndef XGEOM_STATIC_EDITOR_PREVIEW_H
#define XGEOM_STATIC_EDITOR_PREVIEW_H
#pragma once

// The Static Geom editor's 3D preview: the geometry lit with a shadow map (the light's view is rendered first, into its own
// texture), plus wire frame, tangent/binormal/normal lines and a grid. RenderShadow runs before the frame's UI is drawn (it opens its
// own render pass on the window); Draw runs from the panel's render callback, inside the window's own pass.
#include "source/Examples/E19_MaterialEditor/E19_mesh_manager.h"
#include "source/tools/xgpu_imgui_breach.h"
#include "source/tools/xgpu_xcore_bitmap_helpers.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_runtime.h"
#include "plugins/xmaterial.plugin/source/xmaterial_runtime.h"
#include "plugins/xgeom_static.plugin/source/Editor/xgeom_static_editor_inspectors.h"

#include <array>
#include <cstdio>

namespace xgeom_static_editor::preview
{
    constexpr static std::uint32_t g_GeomStaticFragShader[] =
    {
        #include "GeomStaticBasicShader_frag.h"
    };
    constexpr static std::uint32_t g_GeomStaticVertShader[] =
    {
        #include "GeomStaticBasicShader_vert.h"
    };
    constexpr static std::uint32_t g_ShadowFragShader[] =
    {
        #include "GeomStaticShadowMapCreation_frag.h"
    };
    constexpr static std::uint32_t g_ShadowVertShader[] =
    {
        #include "GeomStaticShadowMapCreation_vert.h"
    };
    constexpr static std::uint32_t g_GridVertShader[] =
    {
        #include "E21_GridShader_vert.h"
    };
    constexpr static std::uint32_t g_GridFragShader[] =
    {
        #include "E21_GridShader_frag.h"
    };
    constexpr static std::uint32_t g_WireframeFragShader[] =
    {
        #include "E21_WireFrame_frag.h"
    };
    constexpr static std::uint32_t g_WireframeVertShader[] =
    {
        #include "E21_WireFrame_vert.h"
    };
    constexpr static std::uint32_t g_WireframeGeomShader[] =
    {
        #include "E21_WireFrame_geom.h"
    };
    constexpr static std::uint32_t g_DebugNormalFragShader[] =
    {
        #include "E21_DebugNormalRender_frag.h"
    };
    constexpr static std::uint32_t g_DebugNormalVertShader[] =
    {
        #include "E21_DebugNormalRender_vert.h"
    };
    constexpr static std::uint32_t g_DebugNormalGeomShader[] =
    {
        #include "E21_DebugNormalRender_geom.h"
    };

    struct alignas(256) ubo_geom_static_mesh
    {
        xmath::fmat4  m_L2w;
        xmath::fmat4  m_w2C;
        xmath::fmat4  m_w2ShadowT;
    };

    struct alignas(256) ubo_lighting
    {
        xmath::fvec4  m_LightColor;
        xmath::fvec4  m_AmbientLightColor;
        xmath::fvec4  m_wSpaceLightPos;
        xmath::fvec4  m_wSpaceEyePos;
        xmath::fvec4  m_LightParams;            // .x=area radius (spec/soft shadow approx), .y=temperature (Kelvin), .z=intensity mult, .w=spare
    };

    struct alignas(256) ubo_debug_normal
    {
        xmath::fmat4 m_L2C;
        xmath::fvec4 m_ScaleFactor;
        xmath::fvec4 m_Color;
    };

    struct ubo_shadow_generation
    {
        xmath::fmat4    m_L2C;
    };

    struct alignas(256) ubo_wireframe
    {
        xmath::fmat4  m_L2w;
        xmath::fmat4  m_w2C;
    };

    struct alignas(256) ubo_grid
    {
        xmath::fmat4    m_L2W;
        xmath::fmat4    m_W2C;
        xmath::fmat4    m_L2CTShadow;
        xmath::fvec3    m_WorldSpaceCameraPos = xmath::fvec3(0.0f, 10.0f, 0.0f);
        float           m_MajorGridDiv = 10.0f;
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

        xgpu::vertex_descriptor     m_PrimitiveVD, m_GeomVD, m_ShadowVD;
        xgpu::buffer                m_MeshUBO, m_LightUBO, m_NormalUBO, m_ShadowUBO, m_WireUBO, m_GridUBO;
        xgpu::pipeline              m_Pipeline3D, m_NormalPipeline, m_ShadowPipeline, m_WirePipeline, m_GridPipeline;
        xgpu::pipeline_instance     m_NormalInstance, m_ShadowInstance, m_WireInstance, m_GridInstance;
        xgpu::renderpass            m_ShadowPass;
        xgpu::texture               m_ShadowMap;
        xgpu::texture               m_DefaultTexture;
        e19::mesh_manager           m_Meshes;

        // One entry per material of the compiled geometry; an entry stays invalid when its material could not be built.
        std::vector<xgpu::pipeline_instance>        m_MatInstances;
        std::vector<bool>                           m_MatValid;
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
            m_Meshes.Init(Device);

            auto UBO = [&](xgpu::buffer& B, int Size, int Count) { return Ok(Device.Create(B, { .m_Type = xgpu::buffer::type::UNIFORM, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = Size, .m_EntryCount = Count })); };
            if (!UBO(m_MeshUBO,   sizeof(ubo_geom_static_mesh),  100) || !UBO(m_LightUBO, sizeof(ubo_lighting),        100)
             || !UBO(m_NormalUBO, sizeof(ubo_debug_normal),      100) || !UBO(m_ShadowUBO,sizeof(ubo_shadow_generation),100)
             || !UBO(m_WireUBO,   sizeof(ubo_wireframe),         100) || !UBO(m_GridUBO,  sizeof(ubo_grid),             10)) return false;

            // Vertex layouts: the grid's primitive, the compiled geometry (position stream + extras stream), position only for the shadow/wire passes
            {
                auto Attributes = std::array
                { xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e19::draw_vert, m_X),     .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
                , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e19::draw_vert, m_U),     .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D }
                , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e19::draw_vert, m_Color), .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED }
                };
                if (!Ok(Device.Create(m_PrimitiveVD, xgpu::vertex_descriptor::setup{ .m_VertexSize = sizeof(e19::draw_vert), .m_Attributes = Attributes }))) return false;
            }
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
            auto LightBind   = xgpu::pipeline::uniform_binds{ .m_BindIndex = 1, .m_Usage = { .m_bFragment = true }, .m_Type = xgpu::pipeline::uniform_binds::type::UBO_DYNAMIC };
            auto ClusterBind = xgpu::pipeline::uniform_binds{ .m_BindIndex = 0, .m_Usage = xgpu::shader::type{ xgpu::shader::type::bit::VERTEX }, .m_Type = xgpu::pipeline::uniform_binds::type::SSBO_STATIC };

            // Geometry with no material of its own
            {
                xgpu::shader Frag, Vert;
                if (!Ok(Device.Create(Frag, ShaderSetup(xgpu::shader::type::bit::FRAGMENT, g_GeomStaticFragShader)))) return false;
                if (!Ok(Device.Create(Vert, ShaderSetup(xgpu::shader::type::bit::VERTEX,   g_GeomStaticVertShader)))) return false;

                auto Shaders  = std::array<const xgpu::shader*, 2>{ &Frag, &Vert };
                auto Clamp    = xgpu::pipeline::sampler{ .m_AddressMode = std::array{ xgpu::pipeline::sampler::address_mode::CLAMP, xgpu::pipeline::sampler::address_mode::CLAMP, xgpu::pipeline::sampler::address_mode::CLAMP } };
                auto Samplers = std::array{ Clamp, xgpu::pipeline::sampler{}, xgpu::pipeline::sampler{}, xgpu::pipeline::sampler{} };      // shadow map, normal, albedo, ORME
                auto Binds    = std::array{ MeshBind, LightBind, ClusterBind };
                if (!Ok(Device.Create(m_Pipeline3D, xgpu::pipeline::setup{ .m_VertexDescriptor = m_GeomVD, .m_Shaders = Shaders, .m_PushConstantsSize = sizeof(push_const), .m_UniformBinds = Binds, .m_Samplers = Samplers }))) return false;
            }

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

            if (!Ok(Device.Create(m_ShadowMap, { .m_Format = xgpu::texture::format::DEPTH_U16, .m_Width = 1024, .m_Height = 1024, .m_isGamma = false }))) return false;
            {
                std::array<xgpu::renderpass::attachment, 1> Attachments{ m_ShadowMap };
                if (!Ok(Device.Create(m_ShadowPass, { .m_Attachments = Attachments }))) return false;
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

            // Grid (receives the shadow)
            {
                xgpu::shader Vert, Frag;
                if (!Ok(Device.Create(Vert, ShaderSetup(xgpu::shader::type::bit::VERTEX,   g_GridVertShader)))) return false;
                if (!Ok(Device.Create(Frag, ShaderSetup(xgpu::shader::type::bit::FRAGMENT, g_GridFragShader)))) return false;

                auto Binds    = std::array{ xgpu::pipeline::uniform_binds{ .m_BindIndex = 0, .m_Usage = { .m_bVertex = true, .m_bFragment = true }, .m_Type = xgpu::pipeline::uniform_binds::type::UBO_DYNAMIC } };
                auto Samplers = std::array{ xgpu::pipeline::sampler{ .m_AddressMode = std::array{ xgpu::pipeline::sampler::address_mode::CLAMP, xgpu::pipeline::sampler::address_mode::CLAMP, xgpu::pipeline::sampler::address_mode::CLAMP } } };
                auto Shaders  = std::array<const xgpu::shader*, 2>{ &Frag, &Vert };
                if (!Ok(Device.Create(m_GridPipeline, xgpu::pipeline::setup{ .m_VertexDescriptor = m_PrimitiveVD, .m_Shaders = Shaders, .m_UniformBinds = Binds, .m_Samplers = Samplers, .m_Blend = xgpu::pipeline::blend::getAlphaOriginal() }))) return false;

                auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ m_ShadowMap } };
                if (!Ok(Device.Create(m_GridInstance, { .m_PipeLine = m_GridPipeline, .m_SamplersBindings = Bindings }))) return false;
            }

            // What geometry without a material is drawn with
            if (auto* pErr = xgpu::tools::bitmap::Create(m_DefaultTexture, Device, xbitmap::getDefaultBitmap()); pErr) return false;

            m_bReady = true;
            return true;
        }

        // The pipeline instances the geometry's materials draw with (called after every (re)load of the compiled resource).
        void RebuildMaterials(geom& Geom) noexcept
        {
            ReleaseMaterials();
            const auto Materials = Geom.getDefaultMaterialInstances();
            m_MatInstances.resize(Materials.size());
            m_MatRefs.resize(Materials.size());
            m_MatValid.assign(Materials.size(), false);

            for (auto& MI : Materials)
            {
                const auto Index = static_cast<int>(&MI - Materials.data());
                xresource::g_Mgr.CloneRef(m_MatRefs[Index], MI);

                if (MI.empty())
                {
                    auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ m_ShadowMap }, xgpu::pipeline_instance::sampler_binding{ m_DefaultTexture }
                                              , xgpu::pipeline_instance::sampler_binding{ m_DefaultTexture }, xgpu::pipeline_instance::sampler_binding{ m_DefaultTexture } };
                    m_MatValid[Index] = Ok(m_pDevice->Create(m_MatInstances[Index], { .m_PipeLine = m_Pipeline3D, .m_SamplersBindings = Bindings }));
                    continue;
                }

                xmaterial_instance::rt* pMI = xresource::g_Mgr.getResource(m_MatRefs[Index]);
                if (!pMI) continue;
                auto* pMat = xresource::g_Mgr.getResource(pMI->m_MaterialRef);
                if (!pMat) continue;

                // Slot 0 of every material is the shadow map; the others are the material instance's textures
                std::vector<xgpu::pipeline_instance::sampler_binding> Binds;
                Binds.reserve(pMI->m_nTexturesList);
                bool bTextures = true;
                for (auto& E : pMI->getTextures())
                {
                    if (&E == pMI->getTextures().data()) { Binds.emplace_back(m_ShadowMap); continue; }
                    auto* pTexture = xresource::g_Mgr.getResource(E.m_TexureRef);
                    if (!pTexture) { bTextures = false; break; }
                    Binds.emplace_back(*pTexture);
                }
                if (!bTextures) continue;

                // A material's pipeline is built the first time it is used with static geometry
                auto& PipeLine = pMat->getPipeline(0);
                if (!PipeLine.m_Private)
                {
                    xgpu::shader Vert;
                    if (!Ok(m_pDevice->Create(Vert, ShaderSetup(xgpu::shader::type::bit::VERTEX, g_GeomStaticVertShader)))) continue;

                    std::vector<xgpu::pipeline::sampler> Samplers;
                    Samplers.reserve(Binds.size());
                    for (size_t i = 0; i < Binds.size(); ++i)
                    {
                        if (i == 0) Samplers.push_back(xgpu::pipeline::sampler{ .m_AddressMode = std::array{ xgpu::pipeline::sampler::address_mode::CLAMP, xgpu::pipeline::sampler::address_mode::CLAMP, xgpu::pipeline::sampler::address_mode::CLAMP } });
                        else        Samplers.push_back({});
                    }

                    auto Shaders = std::array<const xgpu::shader*, 2>{ &pMat->getShader(), &Vert };
                    auto Binds3D = std::array{ xgpu::pipeline::uniform_binds{ .m_BindIndex = 0, .m_Usage = { .m_bVertex = true },   .m_Type = xgpu::pipeline::uniform_binds::type::UBO_DYNAMIC }
                                             , xgpu::pipeline::uniform_binds{ .m_BindIndex = 1, .m_Usage = { .m_bFragment = true }, .m_Type = xgpu::pipeline::uniform_binds::type::UBO_DYNAMIC }
                                             , xgpu::pipeline::uniform_binds{ .m_BindIndex = 0, .m_Usage = xgpu::shader::type{ xgpu::shader::type::bit::VERTEX }, .m_Type = xgpu::pipeline::uniform_binds::type::SSBO_STATIC } };
                    if (!Ok(m_pDevice->Create(PipeLine, xgpu::pipeline::setup{ .m_VertexDescriptor = m_GeomVD, .m_Shaders = Shaders, .m_PushConstantsSize = sizeof(push_const), .m_UniformBinds = Binds3D, .m_Samplers = Samplers }))) continue;
                }

                m_MatValid[Index] = Ok(m_pDevice->Create(m_MatInstances[Index], { .m_PipeLine = PipeLine, .m_SamplersBindings = Binds }));
            }
        }

        void ReleaseMaterials() noexcept
        {
            if (m_pDevice) for (auto& E : m_MatInstances) m_pDevice->Destroy(std::move(E));
            for (auto& E : m_MatRefs) xresource::g_Mgr.ReleaseRef(E);
            m_MatInstances.clear();
            m_MatRefs.clear();
            m_MatValid.clear();
        }

        ~runtime() noexcept { ReleaseMaterials(); }

        // Right drag turns the camera, middle drag pans, the wheel zooms, Space lets the light follow the camera.
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

            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) S.m_LightFollowsCamera = !S.m_LightFollowsCamera;
        }

        // The light's view: the geometry into the shadow map. Opens its own render pass on the window, so it must run before the frame's
        // UI is rendered.
        void RenderShadow(xgpu::window& Window, geom& Geom, float ViewW, float ViewH) noexcept
        {
            if (!m_bReady || ViewW <= 1.f || ViewH <= 1.f) return;
            auto& S = m_Settings;

            S.m_LightingView.setViewport({ 0, 0, m_ShadowMap.getTextureDimensions()[0], m_ShadowMap.getTextureDimensions()[1] });
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

            auto CmdBuffer = Window.StartRenderPass(m_ShadowPass);

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

        // The lit geometry with the wire frame, debug lines and the grid over it. From inside the panel's render callback.
        void Draw(xgpu::cmd_buffer& CmdBuffer, geom& Geom, float ViewW, float ViewH) noexcept
        {
            if (!m_bReady || ViewW <= 1.f || ViewH <= 1.f) return;
            auto& S = m_Settings;

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

            // Everything is drawn relative to the camera
            const auto L2w = xmath::fmat4::fromTranslation(-S.m_View.getPosition()) * xmath::fmat4::fromTranslation({ 0, 0, 0 });
            const auto w2C = S.m_View.getW2C() * xmath::fmat4::fromTranslation(S.m_View.getPosition());

            // The lit geometry
            {
                CmdBuffer.setStreamingBuffers({ &Geom.IndexBuffer(), 3 });

                auto& MeshUBO       = m_MeshUBO.allocEntry<ubo_geom_static_mesh>();
                MeshUBO.m_L2w       = L2w;
                MeshUBO.m_w2C       = w2C;
                MeshUBO.m_w2ShadowT = ClipToTexture * m_ShadowL2C;

                auto& Lighting               = m_LightUBO.allocEntry<ubo_lighting>();
                Lighting.m_LightColor        = xmath::fvec4(1) * 4;
                Lighting.m_AmbientLightColor = xmath::fvec4(1) * 0.7f;
                Lighting.m_wSpaceLightPos    = xmath::fvec4(S.m_LightPosition - S.m_View.getPosition(), Geom.m_BBox.getRadius() * 5);
                Lighting.m_wSpaceEyePos      = xmath::fvec4(0);
                Lighting.m_LightParams.m_X   = Lighting.m_wSpaceLightPos.m_W * 0.1f;
                Lighting.m_LightParams.m_Y   = 6500;         // temperature
                Lighting.m_LightParams.m_Z   = 1;            // intensity boost
                Lighting.m_LightParams.m_W   = 0;

                for (auto& M : Geom.getMeshes())
                    for (auto& L : Geom.getLODs().subspan(M.m_iLOD, M.m_nLODs))
                        for (auto& Sub : Geom.getSubmeshes().subspan(L.m_iSubmesh, L.m_nSubmesh))
                        {
                            if (Sub.m_iMaterial >= m_MatValid.size() || !m_MatValid[Sub.m_iMaterial]) continue;

                            std::array StaticUBO{ &Geom.ClusterBuffer() };
                            CmdBuffer.setPipelineInstance(m_MatInstances[Sub.m_iMaterial], StaticUBO);
                            CmdBuffer.setDynamicUBO(m_MeshUBO, 0);
                            CmdBuffer.setDynamicUBO(m_LightUBO, 1);

                            push_const PushConstant{ Sub.m_iCluster };
                            for (auto& C : Geom.getClusters().subspan(Sub.m_iCluster, Sub.m_nCluster))
                            {
                                CmdBuffer.setPushConstants(PushConstant);
                                CmdBuffer.Draw(C.m_nIndices, C.m_iIndex, C.m_iVertex);
                                PushConstant.m_ClusterIndex++;
                            }
                        }
            }

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
                CmdBuffer.setPipelineInstance(m_GridInstance);

                S.m_GridYMin = S.m_bGridToYMin ? Geom.m_BBox.m_Min.m_Y : 0;
                auto& Uniform = m_GridUBO.allocEntry<ubo_grid>();
                Uniform.m_WorldSpaceCameraPos = S.m_View.getPosition();
                Uniform.m_L2W        = xmath::fmat4(xmath::fvec3(100.f, 100.0f, 1.f), xmath::radian3(-90_xdeg, 0_xdeg, 0_xdeg), xmath::fvec3(0, S.m_GridYMin, 0));
                Uniform.m_W2C        = S.m_View.getW2C();
                Uniform.m_L2CTShadow = ClipToTexture * m_ShadowL2C * Uniform.m_L2W;
                CmdBuffer.setDynamicUBO(m_GridUBO, 0);
                m_Meshes.Rendering(CmdBuffer, e19::mesh_manager::model::PLANE3D);

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
