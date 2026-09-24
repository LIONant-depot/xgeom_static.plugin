#ifndef XGEOM_STATIC_THUMBNAIL_H
#define XGEOM_STATIC_THUMBNAIL_H
#pragma once

// Static Geom's thumbnail renderer: the SAME lit-mesh runtime the interactive editor preview panel
// already uses (xgeom_static_editor::preview::runtime - shading/materials/shadow, all reused as-is,
// not reimplemented), just pointed at a fixed 3/4 angle with an exact-fit camera distance instead of
// the panel's own orbit camera - see xeditor_thumbnail_camera_fit.h for why a fixed angle can (and,
// per the user's own ask, should) fit tighter than that panel's angle-agnostic bounding-sphere auto-
// fit ever does.
//
// Owns its own small offscreen colour+depth render target and render pass (built once in Init(), reused
// every call) and its own deferred GPU readback - see xeditor_thumbnail.h's contract comment for why
// (each renderer is now fully self-contained; the shared cache only serializes the finished bitmap it
// hands back). The depth attachment specifically belongs here, not in the shared cache: m_Runtime's
// pipelines reuse the interactive preview's own depth-test-ON state, and only a 3D mesh renderer like
// this one actually needs a working depth buffer to sort overlapping triangles - a flat/unlit type
// (Texture) never enables depth-test on its own pipeline, so it never needed one.
#include "source/Tools/Editor/xeditor_thumbnail.h"
#include "source/Tools/Editor/xeditor_thumbnail_camera_fit.h"
#include "source/Tools/Editor/xeditor_resource_editor.h"
#include "plugins/xgeom_static.plugin/source/Editor/xgeom_static_editor_preview.h"

#include <list>
#include <unordered_map>

namespace xgeom_static_editor
{
    class thumbnail_renderer final : public xeditor::thumbnail_renderer
    {
    public:
        static constexpr float s_CellPixels = 128.0f;   // matches xeditor_thumbnail_cache::s_CellPixels

        bool Init(xgpu::device& Device) noexcept override
        {
            if (m_bReady) return true;
            if (!m_Runtime.Init(Device)) return false;

            const int Cell = static_cast<int>(s_CellPixels);
            if (!Ok(Device.Create(m_ColorTarget, { .m_Format = xgpu::texture::format::R8G8B8A8_UNORM, .m_Width = Cell, .m_Height = Cell, .m_isGamma = false }))) return false;
            if (!Ok(Device.Create(m_DepthTarget, { .m_Format = xgpu::texture::format::DEPTH_U16,      .m_Width = Cell, .m_Height = Cell, .m_isGamma = false }))) return false;
            {
                auto Attachments = std::array<xgpu::renderpass::attachment, 2>{ { m_ColorTarget, m_DepthTarget } };
                if (!Ok(Device.Create(m_Pass, { .m_Attachments = Attachments }))) return false;
            }

            m_bReady = true;
            return true;
        }

        // Polled once per tick (see xeditor_thumbnail.h's contract) until it returns true with OutBitmap
        // filled in. Serialized to one guid at a time (m_ColorTarget/m_DepthTarget are a single reused
        // render target, not one per in-flight guid) - a call for a different guid while still busy just
        // says "not yet".
        bool Render(xgpu::device& Device, xgpu::window& Window, xresource::full_guid Guid, xbitmap& OutBitmap) noexcept override
        {
            if (!m_bReady) return false;

            if (m_bBusy)
            {
                if (m_BusyGuid != Guid) return false;              // busy with someone else - try again later
                if (!m_bReadbackDone) return false;                // still waiting for this frame's PageFlip
                m_bBusy = false;
                if (m_ReadbackWidth != static_cast<int>(s_CellPixels) || m_ReadbackHeight != static_cast<int>(s_CellPixels)) return false;

                // No CPU row-flip: a real camera render (xgpu::tools::view's own Y-up NDC convention)
                // needs no compensation at all once nothing else tries to compensate for it either - see
                // xtexture_thumbnail.h's DrawCube, which used to need a matching GPU-side mirror
                // specifically to cancel out the shared cache's OLD, now-removed CPU flip; two flips or
                // zero flips land on the same correct image, one does not, so dropping both here is the
                // fix, not a shared "apply this matrix everywhere" helper.
                OutBitmap.CreateBitmap(static_cast<int>(s_CellPixels), static_cast<int>(s_CellPixels));
                auto Dst = OutBitmap.getMip<xcolori>(0);
                std::memcpy(Dst.data(), m_ReadbackPixels.data(), Dst.size() * sizeof(xcolori));
                return true;
            }

            auto* pGeom = Reference(Guid);
            if (!pGeom) return false;

            // Rebuilding every call is fine - thumbnail generation is already rate-limited/rare
            // (xtexture_thumbnail.h's own comment applies here too, for the same reason).
            m_Runtime.RebuildMaterials(*pGeom);

            auto& S = m_Runtime.m_Settings;
            S.clear();
            S.m_View.setFov(20_xdeg);
            S.m_View.setAspect(1.0f);
            S.m_View.setViewport({ 0, 0, static_cast<int>(s_CellPixels), static_cast<int>(s_CellPixels) });
            S.m_Angles.m_Pitch = -30_xdeg;
            S.m_Angles.m_Yaw   =  45_xdeg;
            S.m_CameraTarget   = pGeom->m_BBox.getCenter();
            S.m_Distance       = xeditor::ComputeTightFitDistance(S.m_View, S.m_Angles, S.m_CameraTarget, pGeom->m_BBox.m_Min, pGeom->m_BBox.m_Max);

            // The light's view of the geometry, into m_Runtime's own m_ShadowMap - a SEPARATE, SEQUENTIAL
            // render pass (opens and fully ends via RenderShadow's own scope before this call returns),
            // not nested inside our own pass below. The earlier crash ("assert(m_BeginState == 1)" in
            // xgpu_vulkan_window.cpp) came from calling RenderShadow from INSIDE an already-open pass
            // handed in by the old shared cache; now that this renderer owns and sequences its own passes
            // end-to-end, shadow-then-main is exactly the same two-separate-passes-per-frame shape the
            // interactive preview panel already uses (RenderShadow, then later Draw, from RenderPreview()).
            m_Runtime.RenderShadow(Window, *pGeom, s_CellPixels, s_CellPixels);

            {
                // cmd_buffer's own destructor ends the render pass (same RAII shape E22_FramebufferTarget.cpp
                // relies on) - scoped so it ends BEFORE the readback below is requested.
                auto CmdBuffer = Window.StartRenderPass(m_Pass);
                m_Runtime.Draw(CmdBuffer, *pGeom, s_CellPixels, s_CellPixels);
            }

            (void)Window.ReadbackTexture(m_ColorTarget, m_ReadbackPixels, m_ReadbackWidth, m_ReadbackHeight, m_bReadbackDone);
            m_bBusy    = true;
            m_BusyGuid = Guid;
            return false;
        }

    private:
        static bool Ok(xgpu::device::error* pErr) noexcept
        {
            if (!pErr) return true;
            std::printf("Static Geom thumbnail: %s\n", std::string(xgpu::getErrorMsg(pErr)).c_str());
            return false;
        }

        // Keeps the geometry loaded for a little while after its last thumbnail request - mirrors
        // xtexture_thumbnail.h's own Reference()/LRU exactly, for the same reason (a render recorded
        // now is only consumed, and read back, by the GPU several frames later).
        xgeom_static::xgpu::geom* Reference(const xresource::full_guid& Guid) noexcept
        {
            if (auto It = m_Refs.find(Guid); It != m_Refs.end())
            {
                m_Order.remove(Guid);
                m_Order.push_front(Guid);
                return xresource::g_Mgr.getResource(It->second);
            }

            xrsc::geom_static Ref;
            Ref.m_Instance = Guid.m_Instance;
            auto& Held = m_Refs.emplace(Guid, Ref).first->second;
            m_Order.push_front(Guid);
            while (m_Refs.size() > m_Capacity)
            {
                const auto Oldest = m_Order.back();
                if (auto It = m_Refs.find(Oldest); It != m_Refs.end()) { xresource::g_Mgr.ReleaseRef(It->second); m_Refs.erase(It); }
                m_Order.pop_back();
            }
            return xresource::g_Mgr.getResource(Held);
        }

        preview::runtime                                         m_Runtime;
        bool                                                     m_bReady = false;

        xgpu::texture                                            m_ColorTarget;
        xgpu::texture                                            m_DepthTarget;
        xgpu::renderpass                                         m_Pass;
        bool                                                     m_bBusy          = false;
        xresource::full_guid                                     m_BusyGuid       {};
        std::vector<std::uint32_t>                               m_ReadbackPixels;
        int                                                      m_ReadbackWidth  = 0;
        int                                                      m_ReadbackHeight = 0;
        bool                                                     m_bReadbackDone  = false;

        std::size_t                                              m_Capacity = 20;
        std::unordered_map<xresource::full_guid, xrsc::geom_static> m_Refs;
        std::list<xresource::full_guid>                          m_Order;
    };
    // Registered next to g_Registration in xgeom_static_editor.h (this header only defines the class).
}

#endif // XGEOM_STATIC_THUMBNAIL_H
