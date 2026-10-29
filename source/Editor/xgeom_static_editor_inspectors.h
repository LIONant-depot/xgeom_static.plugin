#ifndef XGEOM_STATIC_EDITOR_INSPECTORS_H
#define XGEOM_STATIC_EDITOR_INSPECTORS_H
#pragma once

// What the Static Geom editor shows next to the descriptor: the view settings (camera, light, debug geometry, grid) and the
// read-only facts about the compiled geometry (sizes, counts, meshes, vertex streams, materials).
#include "source/Tools/xgpu_view.h"
#include "dependencies/xeditor_tools/src/xeditor_tools_camera.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xmath/source/bridge/xmath_to_xproperty.h"
#include "dependencies/xresource_mgr/source/xresource_mgr.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"
#include "plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"
#include "plugins/xgeom_static.plugin/source/xgeom_static_xgpu_rsc_loader.h"
#include "plugins/xgeom_static.plugin/source/xgeom_static_xgpu_runtime.h"
#include "source/Tools/Editor/xeditor_geom_facts.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace xgeom_static_editor
{
    // Number formatting and the mesh tree of the facts: shared with the other geometry editors
    using xeditor::geom_facts::AddCommas;
    using xeditor::geom_facts::FormatNumber;
    using xeditor::geom_facts::FormatDistance;

    //---------------------------------------------------------------------------------------------
    // View settings
    //---------------------------------------------------------------------------------------------
    struct render_settings
    {
        void clear()
        {
            m_View = {};
            m_View.setFov(60_xdeg);
            m_CameraTarget = { 0, 0, 0 };
            m_Angles = {};
            m_Distance = -1;    // -1: fit the geometry the next time it draws

            m_LightingView = {};
            m_LightingView.setFov(46_xdeg);
            m_LightDirection = { -3.14f, -1.81f, -3.78f };
            m_LightPosition  = {  3.14f,  1.81f,  3.7f };
            m_LightDirection.NormalizeSafe();

            m_bWireFrame = m_bTangents = m_bBinormals = m_bNormals = m_LightFollowsCamera = false;
            m_GIView      = gi_view::OFF;
            m_GISaturation = 1.0f;
            m_Environment = {};
            m_bGridToYMin = true;
            m_GridYMin    = {};
        }

        void Recenter()
        {
            m_Distance = -1;
            m_CameraTarget = { 0, 0, 0 };
            m_Angles = {};
        }

        xgpu::tools::view   m_View;
        xmath::radian3      m_Angles;
        float               m_Distance;
        xmath::fvec3        m_CameraTarget;
        xeditor_tools::camera_state m_Camera;                // Orbit or Fly, the fly speed (view state, not reflected)
        xgpu::tools::view   m_LightingView;
        xmath::fvec3        m_LightDirection;
        xmath::fvec3        m_LightPosition;
        float               m_ShadowDetailDistance = 2.0f;      // the sun's shadow levels: how many times farther each keeps its detail
        float               m_ShadowLevelBlend     = 0.95f;     // ... the share of a level that blends into the next
        float               m_SunSourceAngle       = 0.4f;      // the sun's disc, degrees (its shadows' softness with the distance to the caster; 0: hard)
        bool                m_bWireFrame;
        bool                m_bTangents;
        bool                m_bBinormals;
        bool                m_bNormals;
        enum class gi_view : std::uint8_t { OFF, GI, SURFELS, LIGHT, SURFACE_COLOR, COVERAGE };
        static constexpr auto gi_view_v = std::array
        { xproperty::settings::enum_item("OFF",     gi_view::OFF,     "No GI: the key light and the flat fill only")
        , xproperty::settings::enum_item("GI",      gi_view::GI,      "Lit with the GI of the geometry's own surfels (the sky and the key light bounce)")
        , xproperty::settings::enum_item("SURFELS", gi_view::SURFELS, "Every pixel in the color of the surfel it reads most: the surfels' placement and reach (black: no surfel reaches it)")
        , xproperty::settings::enum_item("LIGHT",   gi_view::LIGHT,   "The GI light alone: no textures, no direct light")
        , xproperty::settings::enum_item("SURFACE_COLOR", gi_view::SURFACE_COLOR, "Every pixel in the surface color of the surfel it reads most (the material capture): the color the GI bounces")
        , xproperty::settings::enum_item("COVERAGE", gi_view::COVERAGE, "Which GI read lights each pixel: green its own surfels (per pixel), blue the vertices' (per pixel unsupported there), magenta none (no GI there)")
        };
        gi_view             m_GIView;
        float               m_GISaturation;     // the GI light's saturation (1: as solved)
        bool                m_LightFollowsCamera;
        xrsc::texture_ref   m_Environment;      // a cubemap: lights the geometry instead of the flat fill (empty: the flat fill)
        bool                m_bGridToYMin;
        float               m_GridYMin;

        XPROPERTY_DEF
        ( "Render Settings", render_settings
        , obj_scope < "Camera Info"
            , obj_member<"Position", +[](render_settings& O)->auto& { static xmath::fvec3 Pos; Pos = O.m_View.getPosition(); return Pos; }, member_help<"Current camera position in world space">>
            , obj_member<"Target",   &render_settings::m_CameraTarget, member_help<"Point the camera is looking at in world space">>
            , obj_member<"Distance", &render_settings::m_Distance, member_help<"The camera's distance to its target (-1: fit the geometry)">>
            , obj_member<"Pitch",    +[](render_settings& O)->auto& { return O.m_Angles.m_Pitch.m_Value; }, member_help<"The camera's pitch around its target, radians">>
            , obj_member<"Yaw",      +[](render_settings& O)->auto& { return O.m_Angles.m_Yaw.m_Value; }, member_help<"The camera's yaw around its target, radians">>
            , obj_action<"Recenter", &render_settings::Recenter, member_help<"Reset camera to center on the geometry with optimal framing">>>
        , obj_scope< "Lighting Info"
            , obj_member<"Direction",           &render_settings::m_LightDirection, member_help<"Light direction vector (normalized) in world space">>
            , obj_member<"Position",            &render_settings::m_LightPosition, member_help<"Light position in world space when not following camera">>
            , obj_member<"ShadowDetailDistance", &render_settings::m_ShadowDetailDistance, member_help<"How far the sharp shadow levels reach: each level switches to the next coarser one this many times farther away (more shadow pages). 1: the minimum detail">>
            , obj_member<"SunSourceAngle",     &render_settings::m_SunSourceAngle, member_help<"The sun's disc, degrees across (the real sun: about 0.53): shadows sharp where the caster touches, softer with the distance to it. 0: hard shadows">>
            , obj_member<"ShadowLevelBlend",   &render_settings::m_ShadowLevelBlend, member_help<"How long the blend between two shadow levels is: the share of a level over which it fades smoothly into the next">>
            , obj_member<"LightFollowsCamera",  &render_settings::m_LightFollowsCamera, member_help<"Has a key of its own (see Project Settings > Keymap: GeomStatic/Preview/LightFollowsCamera).\n"
                                                                                                    "When enabled, the light moves with the camera for consistent shadow direction.\n"
                                                                                                    "Disabled: the light stays where it is.">>
            , obj_member<"GISaturation",        &render_settings::m_GISaturation, member_help<"The saturation of the GI light shown (1: as solved; luminance kept). The surfels and the filter that smooths them average colors toward grey: more brings the bounced colors back. Preview only.">>
            , obj_member<"Environment",         &render_settings::m_Environment, member_help<"A cubemap texture (HDR or not) that lights the geometry: the diffuse and the reflections by roughness, and the GI's sky.\n"
                                                                                          "Empty: the flat fill. A texture that is not a cubemap is ignored.">>
            >
        , obj_scope< "Debug Geometry"
            , obj_member<"WireFrame",    &render_settings::m_bWireFrame, member_help<"Render geometry in wireframe mode instead of shaded">>
            , obj_member<"Tangents",     &render_settings::m_bTangents, member_help<"Render tangent vectors as debug lines (green) for tangent space visualization">>
            , obj_member<"Binormals",    &render_settings::m_bBinormals, member_help<"Render binormal vectors as debug lines (blue) for tangent space visualization">>
            , obj_member<"Normals",      &render_settings::m_bNormals, member_help<"Render normal vectors as debug lines (red) for surface normal visualization">>
            , obj_member<"GI",           &render_settings::m_GIView, member_enum_span<render_settings::gi_view_v>, member_help<"The surfel GI baked into the geometry: lit by it, or its debug views (the surfels, the GI light alone)">>
            >
        , obj_scope < "Grid"
            , obj_member<"SetToGeomYMin", &render_settings::m_bGridToYMin, member_help<"Align grid plane to geometry's minimum Y position (ground level)">>
            , obj_member<"GeomYMin",      &render_settings::m_GridYMin, member_flags<flags::SHOW_READONLY>, member_help<"Y coordinate where the grid plane is positioned when SetToGeomYMin is enabled">>
            >
        )
    };
    XPROPERTY_REG(render_settings)

    //---------------------------------------------------------------------------------------------
    // Facts about the compiled geometry
    //---------------------------------------------------------------------------------------------
    struct static_geom_inspector
    {
        using cluster         = xeditor::geom_facts::cluster;
        using submesh         = xeditor::geom_facts::submesh;
        using lod             = xeditor::geom_facts::lod;
        using mesh            = xeditor::geom_facts::mesh;
        using stream_element  = xeditor::geom_facts::stream_element;
        using stream          = xeditor::geom_facts::stream;

        void Update(xrsc::geom_static& GeomRef, const std::wstring& ResourcePath)
        {
            clear();

            auto& Geom = *xresource::g_Mgr.getResource(GeomRef);
            m_RscGeom.m_Instance = xresource::g_Mgr.getFullGuid(GeomRef).m_Instance;

            std::error_code Ec;
            m_FileSize = static_cast<int>(std::filesystem::file_size(ResourcePath, Ec));

            m_nClusters = static_cast<int>(Geom.m_nClusters);
            m_nVertices = static_cast<int>(Geom.m_nVertices);
            m_nFaces    = static_cast<int>(Geom.m_nIndices / 3);

            m_BBox       = Geom.m_BBox;
            m_nMeshes    = Geom.m_nMeshes;
            m_nLODs      = Geom.m_nLODs;
            m_nSubmeshes = Geom.m_nSubMeshs;
            m_nIndices   = static_cast<int>(Geom.m_nIndices);

            // Where the memory goes (the GPU's buffers: positions, extras, indices, the clusters' decode data; and the GI's)
            m_BytesPositions = static_cast<int>(Geom.getVertices().size_bytes());
            m_BytesExtras    = static_cast<int>(Geom.getVertexExtras().size_bytes());
            m_BytesIndices   = static_cast<int>(Geom.getIndices().size_bytes());
            m_BytesClusters  = static_cast<int>(Geom.getClusterData().size_bytes());

            // The surfel GI baked in it (the compiler's bake: Mode, SurfelSize in the descriptor)
            switch (Geom.m_GIMode)
            {
            case xgeom_static::gi_mode::OFF:                m_GIMode = "OFF";                break;
            case xgeom_static::gi_mode::RECEIVE:            m_GIMode = "RECEIVE";            break;
            case xgeom_static::gi_mode::RECEIVE_AND_BOUNCE: m_GIMode = "RECEIVE_AND_BOUNCE"; break;
            }
            m_nGISurfels = static_cast<int>(Geom.m_nGISurfels);
            m_nGIParents = static_cast<int>(Geom.m_nGIParents);
            if (!Geom.getGISurfels().empty()) m_GISurfelRadius = Geom.getGISurfels()[0].m_Radius;
            if (!Geom.getGIParents().empty()) m_GIParentRadius = Geom.getGIParents()[0].m_Radius;
            m_BytesGISurfels  = static_cast<int>(Geom.getGISurfels().size_bytes() + Geom.getGIParents().size_bytes());
            m_BytesGIVertex   = static_cast<int>(Geom.getGIVertexSurfels().size_bytes());
            m_BytesGIMaterial = static_cast<int>(Geom.getGISurfelMaterials().size_bytes());
            m_BytesGIDerived  = static_cast<int>(Geom.getGIDerived().size_bytes());

            m_VertexStreams.resize(2);
            m_VertexStreams[0].m_Name = "Position";
            m_VertexStreams[0].m_Elements.resize(2);
            m_VertexStreams[0].m_Elements[0] = { "SINT16_3D", "(X,Y,Z) 16 bit normalize positions" };
            m_VertexStreams[0].m_Elements[1] = { "SINT16_1D", "~4bit of extra precisions for TBN encoded in 16bits. bit 15 is the sign of the Binormal" };

            m_VertexStreams[1].m_Name = "Extras";
            m_VertexStreams[1].m_Elements.resize(3);
            m_VertexStreams[1].m_Elements[0] = { "UINT16_2D", "(U,V) 16 bit normalize, texture coordinates packed as 16bits (they can tiled)" };
            m_VertexStreams[1].m_Elements[1] = { "UINT8_2D",  "normal oct compress with 8 bit precision + 4bits from extras in position == 12bits of precision" };
            m_VertexStreams[1].m_Elements[2] = { "UINT8_2D",  "tangent oct compress with 8 bit precision + 3bits from extras in position == 11bits of precision" };

            for (auto& M : Geom.getDefaultMaterialInstances())
                m_Materials.emplace_back(xresource::g_Mgr.getFullGuid(M).m_Instance);

            xeditor::geom_facts::FillMeshes(Geom, m_Mesh, m_Size);
        }

        void clear()
        {
            m_Mesh.clear();
            m_nClusters = m_nVertices = m_nFaces = 0;
            m_nMeshes = m_nLODs = m_nSubmeshes = m_nIndices = 0;
            m_BytesPositions = m_BytesExtras = m_BytesIndices = m_BytesClusters = 0;
            m_GIMode.clear();
            m_nGISurfels = m_nGIParents = 0;
            m_GISurfelRadius = m_GIParentRadius = 0;
            m_BytesGISurfels = m_BytesGIVertex = m_BytesGIMaterial = m_BytesGIDerived = 0;
            m_Materials.clear();
        }

        xrsc::geom_static                           m_RscGeom;
        std::vector<mesh>                           m_Mesh;
        int                                         m_nClusters = 0;
        int                                         m_nVertices = 0;
        int                                         m_nFaces    = 0;
        std::vector<xrsc::material_instance_ref>    m_Materials;
        xmath::fbbox                                m_BBox;
        xmath::fvec3                                m_Size;
        int                                         m_FileSize  = 0;
        int                                         m_nMeshes = 0, m_nLODs = 0, m_nSubmeshes = 0, m_nIndices = 0;
        int                                         m_BytesPositions = 0, m_BytesExtras = 0, m_BytesIndices = 0, m_BytesClusters = 0;
        std::string                                 m_GIMode;
        int                                         m_nGISurfels = 0, m_nGIParents = 0;
        float                                       m_GISurfelRadius = 0, m_GIParentRadius = 0;
        int                                         m_BytesGISurfels = 0, m_BytesGIVertex = 0, m_BytesGIMaterial = 0, m_BytesGIDerived = 0;
        std::vector<stream>                         m_VertexStreams;

        XPROPERTY_DEF
        ( "StaticGeom Inspector", static_geom_inspector
        , obj_member<"RscGeom",         &static_geom_inspector::m_RscGeom, member_flags<flags::SHOW_READONLY>, member_help<"Reference to the static geometry resource">>
        , obj_scope< "Size"
            , obj_member < "X", +[](static_geom_inspector& O, bool bRead, std::string& Val) { if (bRead) Val = FormatDistance(O.m_Size.m_X); }, member_help<"Width of the geometry bounding box">>
            , obj_member < "Y", +[](static_geom_inspector& O, bool bRead, std::string& Val) { if (bRead) Val = FormatDistance(O.m_Size.m_Y); }, member_help<"Height of the geometry bounding box">>
            , obj_member < "Z", +[](static_geom_inspector& O, bool bRead, std::string& Val) { if (bRead) Val = FormatDistance(O.m_Size.m_Z); }, member_help<"Depth of the geometry bounding box">>
            , member_flags<flags::SHOW_READONLY>
            >
        , obj_member<"nClusters", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_nClusters, 0); }, member_flags<flags::SHOW_READONLY>, member_help<"Number of clusters (renderable chunks) in the geometry">>
        , obj_member<"nVertices", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_nVertices, 0); }, member_flags<flags::SHOW_READONLY>, member_help<"Total number of vertices across all LODs">>
        , obj_member<"nFaces",    +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_nFaces, 0);    }, member_flags<flags::SHOW_READONLY>, member_help<"Total number of faces (triangles) across all LODs">>
        , obj_member<"FileSize",  +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_FileSize, 0) + " Bytes"; }, member_flags<flags::SHOW_READONLY>, member_help<"Size of the compiled geometry file on disk">>
        , obj_member<"BBox",      &static_geom_inspector::m_BBox, member_flags<flags::SHOW_READONLY>, member_ui_open<false>, member_help<"Axis-aligned bounding box of the whole geometry (the decode space of its 16 bit positions)">>
        , obj_scope< "Counts"
            , obj_member<"nMeshes", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_nMeshes, 0); }, member_help<"Meshes in the geometry">>
            , obj_member<"nLODs", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_nLODs, 0); }, member_help<"Level of detail versions across the meshes">>
            , obj_member<"nSubmeshes", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_nSubmeshes, 0); }, member_help<"Submeshes (a mesh's part with one material) across the LODs">>
            , obj_member<"nIndices", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_nIndices, 0); }, member_help<"Indices of the 16 bit index buffer (3 per face)">>
            , member_flags<flags::SHOW_READONLY>
            >
        , obj_scope< "Memory"
            , obj_member<"Positions", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_BytesPositions, 0) + " Bytes"; }, member_help<"The position stream: 3 x 16 bit and the extra precision bits per vertex">>
            , obj_member<"Extras", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_BytesExtras, 0) + " Bytes"; }, member_help<"The extras stream: UV, normal and tangent per vertex">>
            , obj_member<"Indices", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_BytesIndices, 0) + " Bytes"; }, member_help<"The index buffer">>
            , obj_member<"ClusterData", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_BytesClusters, 0) + " Bytes"; }, member_help<"Per cluster: the scale and translation that decode its positions and UVs">>
            , member_flags<flags::SHOW_READONLY>
            >
        , obj_scope< "GI"
            , obj_member<"Mode", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = O.m_GIMode; }, member_help<"The GI mode this geometry was compiled with (the descriptor's GI Mode)">>
            , obj_member<"nSurfels", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_nGISurfels, 0); }, member_help<"Fine surfels baked in the geometry: the light the geometry receives is solved at these (the descriptor's GI SurfelSize sets how many)">>
            , obj_member<"nParents", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_nGIParents, 0); }, member_help<"Parent surfels (3x the radius): what a far or small instance holds instead of the fine ones">>
            , obj_member<"SurfelRadius", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_GISurfelRadius, 3) + " mesh units"; }, member_help<"The radius of a fine surfel, in the geometry's own units">>
            , obj_member<"ParentRadius", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_GIParentRadius, 3) + " mesh units"; }, member_help<"The radius of a parent surfel">>
            , obj_member<"SurfelData", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_BytesGISurfels, 0) + " Bytes"; }, member_help<"The fine and the parent surfels (center, radius, normal, parent)">>
            , obj_member<"VertexLinks", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_BytesGIVertex, 0) + " Bytes"; }, member_help<"Per vertex its 4 nearest fine surfels (what the vertex read blends)">>
            , obj_member<"MaterialSamples", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_BytesGIMaterial, 0) + " Bytes"; }, member_help<"Per fine surfel where it samples its material (slot, UV, footprint): how the surfels get their colors">>
            , obj_member<"LoadTimeStructures", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_BytesGIDerived, 0) + " Bytes"; }, member_help<"The read grid and the neighbor lists, built by the compiler and kept so the editor does not build them at load">>
            , member_flags<flags::SHOW_READONLY>
            >
        , obj_member<"Meshes",          &static_geom_inspector::m_Mesh,          member_flags<flags::SHOW_READONLY>, member_help<"List of meshes in the geometry, each containing multiple LODs">>
        , obj_member<"VertexStreams",   &static_geom_inspector::m_VertexStreams, member_flags<flags::SHOW_READONLY>, member_help<"Vertex data layout: Position stream (coordinates) and Extras stream (UV, normals, tangents)">>
        , obj_member<"Materials",       &static_geom_inspector::m_Materials,     member_flags<flags::SHOW_READONLY>, member_help<"References to material instances used by this geometry">>
        );
    };
    XPROPERTY_REG(static_geom_inspector)
}

#endif // XGEOM_STATIC_EDITOR_INSPECTORS_H
