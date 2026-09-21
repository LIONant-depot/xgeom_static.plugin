#ifndef XGEOM_STATIC_EDITOR_INSPECTORS_H
#define XGEOM_STATIC_EDITOR_INSPECTORS_H
#pragma once

// What the Static Geom editor shows next to the descriptor: the view settings (camera, light, debug geometry, grid) and the
// read-only facts about the compiled geometry (sizes, counts, meshes, vertex streams, materials).
#include "source/tools/xgpu_view.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xmath/source/bridge/xmath_to_xproperty.h"
#include "dependencies/xresource_mgr/source/xresource_mgr.h"
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"
#include "plugins/xgeom_static.plugin/source/xgeom_static_xgpu_rsc_loader.h"
#include "plugins/xgeom_static.plugin/source/xgeom_static_xgpu_runtime.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace xgeom_static_editor
{
    //---------------------------------------------------------------------------------------------
    // Number formatting
    //---------------------------------------------------------------------------------------------
    inline std::string AddCommas(const std::string& S) noexcept
    {
        std::string Sign, Int, Frac;
        size_t Pos = 0;
        if (!S.empty() && (S[0] == '-' || S[0] == '+')) { Sign = S.substr(0, 1); Pos = 1; }

        const size_t Dot = S.find('.', Pos);
        if (Dot == std::string::npos) Int = S.substr(Pos);
        else { Int = S.substr(Pos, Dot - Pos); Frac = S.substr(Dot); }

        std::string Out;
        int Count = 0;
        for (int i = static_cast<int>(Int.size()) - 1; i >= 0; --i)
        {
            Out.push_back(Int[i]);
            if (++Count == 3 && i > 0) { Out.push_back(','); Count = 0; }
        }
        std::reverse(Out.begin(), Out.end());
        return Sign + Out + Frac;
    }

    // At most MaxDecimals decimals, trailing zeros trimmed, thousands separated.
    inline std::string FormatNumber(double Value, int MaxDecimals) noexcept
    {
        const double Scale   = std::pow(10.0, static_cast<double>(MaxDecimals));
        const double Rounded = MaxDecimals <= 0 ? std::round(Value) : std::round(Value * Scale) / Scale;

        std::ostringstream Stream;
        Stream.setf(std::ios::fixed);
        Stream.precision(MaxDecimals);
        Stream << Rounded;
        std::string S = Stream.str();

        if (S.find('.') != std::string::npos)
        {
            while (!S.empty() && S.back() == '0') S.pop_back();
            if (!S.empty() && S.back() == '.') S.pop_back();
        }
        return AddCommas(S);
    }

    // A length in meters as the most natural of mm, cm, m, Km.
    inline std::string FormatDistance(float Meters) noexcept
    {
        if (std::isnan(Meters)) return "NaN m";
        if (std::isinf(Meters)) return Meters > 0 ? "INF m" : "-INF m";

        const double V = Meters, Abs = std::fabs(V);
        if (Abs >= 1000.0) return FormatNumber(V / 1000.0, 3) + " Km";
        if (Abs >= 1.0)    return FormatNumber(V,           3) + " m";
        if (Abs >= 0.01)   return FormatNumber(V * 100.0,   3) + " cm";
        return                    FormatNumber(V * 1000.0,  3) + " mm";
    }

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
        xgpu::tools::view   m_LightingView;
        xmath::fvec3        m_LightDirection;
        xmath::fvec3        m_LightPosition;
        bool                m_bWireFrame;
        bool                m_bTangents;
        bool                m_bBinormals;
        bool                m_bNormals;
        bool                m_LightFollowsCamera;
        bool                m_bGridToYMin;
        float               m_GridYMin;

        XPROPERTY_DEF
        ( "Render Settings", render_settings
        , obj_scope < "Camera Info"
            , obj_member<"Position", +[](render_settings& O)->auto& { static xmath::fvec3 Pos; Pos = O.m_View.getPosition(); return Pos; }>
            , obj_member<"Target",   &render_settings::m_CameraTarget >
            , obj_action<"Recenter", &render_settings::Recenter>>
        , obj_scope< "Lighting Info"
            , obj_member<"Direction",           &render_settings::m_LightDirection >
            , obj_member<"Position",            &render_settings::m_LightPosition >
            , obj_member<"LightFollowsCamera",  &render_settings::m_LightFollowsCamera, member_help<"Hotkey: SPACE\n"
                                                                                                    "By using this function the user can move the light in any direction by making the light be the camera"
                                                                                                    "After the user is satisfy it can press SPACE again to freeze the light location."
            >>
            >
        , obj_scope< "Debug Geometry"
            , obj_member<"WireFrame",    &render_settings::m_bWireFrame >
            , obj_member<"Tangents",     &render_settings::m_bTangents >
            , obj_member<"Binormals",    &render_settings::m_bBinormals >
            , obj_member<"Normals",      &render_settings::m_bNormals >
            >
        , obj_scope < "Grid"
            , obj_member<"SetToGeomYMin", &render_settings::m_bGridToYMin >
            , obj_member<"GeomYMin",      &render_settings::m_GridYMin, member_flags<flags::SHOW_READONLY> >
            >
        )
    };
    XPROPERTY_REG(render_settings)

    //---------------------------------------------------------------------------------------------
    // Facts about the compiled geometry
    //---------------------------------------------------------------------------------------------
    struct static_geom_inspector
    {
        struct cluster
        {
            int                         m_nVertices;
            int                         m_nFaces;
            xmath::fbbox                m_BBox;

            XPROPERTY_DEF
            ( "Cluster", cluster
            , obj_member<"nVertices", &cluster::m_nVertices >
            , obj_member<"nFaces",    &cluster::m_nFaces >
            , obj_member<"BBox",      &cluster::m_BBox >
            )
        };

        struct submesh
        {
            int                         m_nVertices;
            int                         m_nFaces;
            xrsc::material_instance_ref m_Material;
            std::vector<cluster>        m_Cluster;

            XPROPERTY_DEF
            ( "Submesh", submesh
            , obj_member<"nVertices", &submesh::m_nVertices >
            , obj_member<"nFaces",    &submesh::m_nFaces >
            , obj_member<"Material",  &submesh::m_Material >
            , obj_member<"Cluster",   &submesh::m_Cluster >
            )
        };

        struct lod
        {
            int                         m_nClusters;
            int                         m_nVertices;
            int                         m_nFaces;
            float                       m_ScreenArea;
            std::vector<submesh>        m_Submesh;

            XPROPERTY_DEF
            ( "lod", lod
            , obj_member<"nClusters",   &lod::m_nClusters >
            , obj_member<"nVertices",   &lod::m_nVertices >
            , obj_member<"nFaces",      &lod::m_nFaces >
            , obj_member<"ScreenArea",  &lod::m_ScreenArea >
            , obj_member<"Submesh",     &lod::m_Submesh >
            )
        };

        struct mesh
        {
            std::string                 m_Name;
            int                         m_nClusters;
            int                         m_nVertices;
            int                         m_nFaces;
            int                         m_nMaterials;
            std::vector<lod>            m_LODs;
            xmath::fbbox                m_BBox;

            XPROPERTY_DEF
            ( "mesh", mesh
            , obj_member<"Name",        &mesh::m_Name >
            , obj_member<"BBox",        &mesh::m_BBox, member_ui_open<false> >
            , obj_member<"nClusters",   &mesh::m_nClusters >
            , obj_member<"nVertices",   &mesh::m_nVertices >
            , obj_member<"nFaces",      &mesh::m_nFaces >
            , obj_member<"nMaterials",  &mesh::m_nMaterials >
            , obj_member<"LODs",        &mesh::m_LODs >
            )
        };

        struct stream_element
        {
            std::string     m_Type;
            std::string     m_Desciption;

            XPROPERTY_DEF
            ( "stream_element", stream_element
            , obj_member<"Type",        &stream_element::m_Type >
            , obj_member<"Desciption",  &stream_element::m_Desciption >
            )
        };

        struct stream
        {
            std::string                 m_Name;
            std::vector<stream_element> m_Elements;

            XPROPERTY_DEF
            ( "stream", stream
            , obj_member<"Name",        &stream::m_Name >
            , obj_member<"Elements",    &stream::m_Elements >
            )
        };

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

            m_Mesh.resize(Geom.m_nMeshes);
            for (auto& M : Geom.getMeshes())
            {
                auto& Mesh = m_Mesh[static_cast<int>(&M - Geom.getMeshes().data())];

                Mesh.m_nClusters = 0;
                Mesh.m_nFaces    = 0;
                Mesh.m_nVertices = 0;
                Mesh.m_nMaterials = 0;
                Mesh.m_BBox      = M.m_BBox;
                Mesh.m_Name      = M.m_Name.data();
                Mesh.m_LODs.resize(M.m_nLODs);

                auto LODSpan = Geom.getLODs().subspan(M.m_iLOD, M.m_nLODs);
                for (auto& L : LODSpan)
                {
                    auto& LOD = Mesh.m_LODs[static_cast<int>(&L - LODSpan.data())];

                    LOD.m_ScreenArea = L.m_ScreenArea;
                    LOD.m_nClusters  = 0;
                    LOD.m_nFaces     = 0;
                    LOD.m_nVertices  = 0;
                    LOD.m_Submesh.resize(L.m_nSubmesh);

                    auto SubmeshSpan = Geom.getSubmeshes().subspan(L.m_iSubmesh, L.m_nSubmesh);
                    for (auto& S : SubmeshSpan)
                    {
                        auto& Sub = LOD.m_Submesh[static_cast<int>(&S - SubmeshSpan.data())];

                        Sub.m_nFaces    = 0;
                        Sub.m_nVertices = 0;
                        Sub.m_Material.m_Instance = xresource::g_Mgr.getFullGuid(Geom.getDefaultMaterialInstances().subspan(S.m_iMaterial, 1)[0]).m_Instance;
                        Sub.m_Cluster.resize(S.m_nCluster);

                        auto ClusterSpan = Geom.getClusters().subspan(S.m_iCluster, S.m_nCluster);
                        for (auto& C : ClusterSpan)
                        {
                            auto& Cluster = Sub.m_Cluster[static_cast<int>(&C - ClusterSpan.data())];

                            Cluster.m_nFaces    = C.m_nIndices / 3;
                            Cluster.m_nVertices = C.m_nVertices;
                            Cluster.m_BBox      = C.m_BBox;

                            Sub.m_nFaces    += Cluster.m_nFaces;
                            Sub.m_nVertices += Cluster.m_nVertices;
                        }

                        LOD.m_nFaces    += Sub.m_nFaces;
                        LOD.m_nVertices += Sub.m_nVertices;
                        LOD.m_nClusters += static_cast<int>(Sub.m_Cluster.size());
                    }

                    Mesh.m_nFaces     += LOD.m_nFaces;
                    Mesh.m_nVertices  += LOD.m_nVertices;
                    Mesh.m_nClusters  += LOD.m_nClusters;
                    Mesh.m_nMaterials += static_cast<int>(LOD.m_Submesh.size());
                }
            }

            m_Size.setup(0);
            for (auto& M : m_Mesh) m_Size = m_Size.Max(M.m_BBox.getSize());
        }

        void clear()
        {
            m_Mesh.clear();
            m_nClusters = m_nVertices = m_nFaces = 0;
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
        std::vector<stream>                         m_VertexStreams;

        XPROPERTY_DEF
        ( "StaticGeom Inspector", static_geom_inspector
        , obj_member<"RscGeom",         &static_geom_inspector::m_RscGeom, member_flags<flags::SHOW_READONLY> >
        , obj_scope< "Size"
            , obj_member < "X", +[](static_geom_inspector& O, bool bRead, std::string& Val) { if (bRead) Val = FormatDistance(O.m_Size.m_X); } >
            , obj_member < "Y", +[](static_geom_inspector& O, bool bRead, std::string& Val) { if (bRead) Val = FormatDistance(O.m_Size.m_Y); } >
            , obj_member < "Z", +[](static_geom_inspector& O, bool bRead, std::string& Val) { if (bRead) Val = FormatDistance(O.m_Size.m_Z); } >
            , member_flags<flags::SHOW_READONLY>
            >
        , obj_member<"nClusters", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_nClusters, 0); }, member_flags<flags::SHOW_READONLY> >
        , obj_member<"nVertices", +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_nVertices, 0); }, member_flags<flags::SHOW_READONLY> >
        , obj_member<"nFaces",    +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_nFaces, 0);    }, member_flags<flags::SHOW_READONLY> >
        , obj_member<"FileSize",  +[](static_geom_inspector& O, bool bRead, std::string& Value) { if (bRead) Value = FormatNumber(O.m_FileSize, 0) + " Bytes"; }, member_flags<flags::SHOW_READONLY> >
        , obj_member<"Meshes",          &static_geom_inspector::m_Mesh,          member_flags<flags::SHOW_READONLY> >
        , obj_member<"VertexStreams",   &static_geom_inspector::m_VertexStreams, member_flags<flags::SHOW_READONLY> >
        , obj_member<"Materials",       &static_geom_inspector::m_Materials,     member_flags<flags::SHOW_READONLY> >
        );
    };
    XPROPERTY_REG2(prop_cluster,            static_geom_inspector::cluster)
    XPROPERTY_REG2(prop_submesh,            static_geom_inspector::submesh)
    XPROPERTY_REG2(prop_lod,                static_geom_inspector::lod)
    XPROPERTY_REG2(prop_mesh,               static_geom_inspector::mesh)
    XPROPERTY_REG2(prop_stream_element,     static_geom_inspector::stream_element)
    XPROPERTY_REG2(prop_stream,             static_geom_inspector::stream)
    XPROPERTY_REG(static_geom_inspector)
}

#endif // XGEOM_STATIC_EDITOR_INSPECTORS_H
