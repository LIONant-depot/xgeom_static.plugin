#ifndef XGEOM_STATIC_THUMBNAIL_H
#define XGEOM_STATIC_THUMBNAIL_H
#pragma once

// Static Geom's thumbnail renderer: the SAME lit-mesh runtime the interactive editor preview panel
// already uses (xgeom_static_editor::preview::runtime - shading/materials/shadow, all reused as-is,
// not reimplemented), just pointed at a fixed 3/4 angle with an exact-fit camera distance instead of
// the panel's own orbit camera - see xeditor_thumbnail_camera_fit.h for why a fixed angle can (and,
// per the user's own ask, should) fit tighter than that panel's angle-agnostic bounding-sphere auto-
// fit ever does.
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
        bool Init(xgpu::device& Device) noexcept override
        {
            return m_Runtime.Init(Device);
        }

        bool Draw(xgpu::device& Device, xgpu::cmd_buffer& CmdBuffer, xresource::full_guid Guid) noexcept override
        {
            if (!m_Runtime.m_bReady) return false;
            auto* pGeom = Reference(Guid);
            if (!pGeom) return false;

            // Rebuilding every call is fine - thumbnail generation is already rate-limited/rare
            // (xtexture_thumbnail.h's own comment applies here too, for the same reason).
            m_Runtime.RebuildMaterials(*pGeom);

            constexpr float CellPixels = 128.0f;   // matches xeditor_thumbnail_cache::s_CellPixels
            auto& S = m_Runtime.m_Settings;
            S.clear();
            S.m_View.setFov(20_xdeg);
            S.m_View.setAspect(1.0f);
            S.m_View.setViewport({ 0, 0, static_cast<int>(CellPixels), static_cast<int>(CellPixels) });
            S.m_Angles.m_Pitch = -30_xdeg;
            S.m_Angles.m_Yaw   =  45_xdeg;
            S.m_CameraTarget   = pGeom->m_BBox.getCenter();
            S.m_Distance       = xeditor::ComputeTightFitDistance(S.m_View, S.m_Angles, S.m_CameraTarget, pGeom->m_BBox.m_Min, pGeom->m_BBox.m_Max);

            // No shadow pass here: RenderShadow() opens its OWN window render pass
            // (Window.StartRenderPass(m_ShadowPass)), but Draw() is only ever called with one already
            // active (the scratch thumbnail target xeditor_thumbnail_cache::Generate() opened before
            // calling us) - the window's render-pass state machine only allows one such pass open at a
            // time (confirmed live: nesting a second StartRenderPass call here trips
            // xgpu_vulkan_window.cpp's "assert(m_BeginState == 1)" and crashes). A zero shadow matrix
            // is the existing "no shadow, always lit" convention every shadow-sampling shader here
            // already honors (see xskeleton_editor_scene.h's own m_ShadowL2C default, and
            // mb_standard_shadow.frag's ShadowPCF: a NaN UVProjection from dividing by a zero W fails
            // its range check and safely falls through to Shadow=1.0) - reset it explicitly since
            // m_Runtime is a persistent, reused-across-resources instance and nothing else ever writes it.
            m_Runtime.m_ShadowL2C = xmath::fmat4::fromZero();

            m_Runtime.Draw(CmdBuffer, *pGeom, CellPixels, CellPixels);
            return true;
        }

    private:
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
        std::size_t                                              m_Capacity = 20;
        std::unordered_map<xresource::full_guid, xrsc::geom_static> m_Refs;
        std::list<xresource::full_guid>                          m_Order;
    };
    // Registered next to g_Registration in xgeom_static_editor.h (this header only defines the class).
}

#endif // XGEOM_STATIC_THUMBNAIL_H
