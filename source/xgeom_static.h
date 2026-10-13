#ifndef XGEOM_STATIC_RUNTIME_H
#define XGEOM_STATIC_RUNTIME_H
#pragma once

#include "dependencies/xmath/source/xmath_fshapes.h"
#include "dependencies/xserializer/source/xserializer.h"
#include <span>  // Add for std::span

namespace xgeom_static
{
    // The GI a geometry takes part in (xLIONRender's surfel GI): none, it receives (its surfels are lit, it is not in the rays' way), or it
    // receives and bounces (the rays of every mesh see it: it occludes and colors its neighbors)
    enum class gi_mode : std::uint8_t { OFF, RECEIVE, RECEIVE_AND_BOUNCE };

    struct geom
    {
        inline static constexpr auto xserializer_version_v = 3;    // 2: the GI surfels, 3: where each samples its material
        struct mesh
        {
            std::array<char, 32>    m_Name;
            float                   m_WorldPixelSize;   // Average World Pixel size for this SubMesh
            xmath::fbbox            m_BBox;
            std::uint16_t           m_nLODs;
            std::uint16_t           m_iLOD;
        };

        struct lod
        {
            float                   m_ScreenArea;
            std::uint16_t           m_iSubmesh;         // Start the submeshes
            std::uint16_t           m_nSubmesh;
        };

        struct submesh
        {
            std::uint16_t           m_iCluster;         // Where the index starts
            std::uint16_t           m_nCluster;         // Where the index starts
            std::uint16_t           m_iMaterial;        // Index of the Material that this SubMesh uses
        };

        struct vec3
        {
            float m_X, m_Y, m_Z;
        };

        struct vec2
        {
            float m_X, m_Y;
        };

        struct vec4
        {
            float m_X, m_Y, m_Z, m_W;
        };

        struct cluster_data
        {
            vec4                    m_PosScaleAndWPADDING;        // XYZ scale for the cluster, W = U Scale
            vec4                    m_PosTrasnlationAndWPADDING;  // XYZ scale for the cluster, W = V Scale
            vec4                    m_UVScaleTranslation;         // UV Translation.
        };

        struct cluster
        {
            xmath::fbbox            m_BBox;                     // Optional fine-grained CPU culling (e.g., per-cluster frustum/occlusion)
            std::uint32_t           m_iIndex;                   // Where the index starts
            std::uint32_t           m_nIndices;                 // number of
            std::uint32_t           m_iVertex;                  // Where the vertex starts
            std::uint32_t           m_nVertices;                // number of
        };

        struct vertex
        {
            int16_t m_XPos, m_YPos, m_ZPos;
            int16_t m_Extra;                            // Bit 0: binormal sign (0:+1, 1:-1)
        };

        struct vertex_extras
        {
            std::array<std::uint16_t,2>     m_UV;
            std::array<std::uint8_t, 2>     m_OctNormal;
            std::array<std::uint8_t, 2>     m_OctTangent;
        };

        // A GI surfel, mesh space (the compiler's bake: xlionrender::gi::surfel_mesh::Bake). The fine ones first, then their parents (the LOD)
        struct gi_surfel
        {
            vec3                    m_Center;
            float                   m_Radius;
            vec3                    m_Normal;
            std::uint32_t           m_iParent;                  // fine: its parent (an index into the parents); parent: 0
        };

        // Where a fine GI surfel samples its material (its color: the material there, over its footprint): the slot, the UV of its center,
        // the UV radius of its footprint
        struct gi_surfel_material
        {
            float                   m_U, m_V, m_UVRadius;
            std::uint32_t           m_iMaterial;
        };

        using runtime_allocation =std::array<std::size_t, 4*(sizeof(std::shared_ptr<int>) / sizeof(std::size_t))>;

        //-------------------------------------------------------------------------

                                                        geom                        (void)                                      noexcept = default;
        inline                                          geom                        (xserializer::stream& Steaming)             noexcept;
        inline void                                     Kill                        (void)                                      noexcept;
        inline void                                     Initialize                  (void)                                      noexcept;
        inline int                                      findMeshIndex               (const char* pName)                 const   noexcept;
        inline int                                      getSubMeshIndex             (int iMesh, int iMaterialInstance)  const   noexcept;
        inline std::span<mesh>                          getMeshes                   (void)                              const   noexcept { return { m_pMesh, m_nMeshes }; }
        inline std::span<lod>                           getLODs                     (void)                              const   noexcept { return { m_pLOD, m_nLODs }; }
        inline std::span<submesh>                       getSubmeshes                (void)                              const   noexcept { return { m_pSubMesh, m_nSubMeshs }; }
        inline std::span<cluster>                       getClusters                 (void)                              const   noexcept { return { m_pCluster, m_nClusters }; }
        inline std::span<vertex>                        getVertices                 (void)                              const   noexcept { return { reinterpret_cast<vertex*>       (m_pData + m_VertexOffset),         m_nVertices }; }
        inline std::span<vertex_extras>                 getVertexExtras             (void)                              const   noexcept { return { reinterpret_cast<vertex_extras*>(m_pData + m_VertexExtrasOffset),   m_nVertices }; }
        inline std::span<std::uint16_t>                 getIndices                  (void)                              const   noexcept { return { reinterpret_cast<std::uint16_t*>(m_pData + m_IndicesOffset),        m_nIndices  }; }
        inline std::span<cluster_data>                  getClusterData              (void)                              const   noexcept { return { reinterpret_cast<cluster_data*> (m_pData + m_ClusterDataOffset),    m_nClusters }; }
        inline std::span<xrsc::material_instance_ref>   getDefaultMaterialInstances (void)                              const   noexcept { return { m_pDefaultMaterialInstances, m_nDefaultMaterialInstances }; }
        inline std::span<gi_surfel>                     getGISurfels                (void)                              const   noexcept { return { m_pGISurfel, m_nGISurfels }; }
        inline std::span<gi_surfel>                     getGIParents                (void)                              const   noexcept { return { m_pGISurfel + m_nGISurfels, m_nGIParents }; }
        inline std::span<gi_surfel_material>            getGISurfelMaterials        (void)                              const   noexcept { return { m_pGISurfelMaterial, m_pGISurfelMaterial ? m_nGISurfels : 0u }; }
        inline std::span<std::uint16_t>                 getGIVertexSurfels          (void)                              const   noexcept { return { m_pGIVertSurfel, m_pGIVertSurfel ? 4 * std::size_t(m_nVertices) : 0 }; }

        xmath::fbbox                    m_BBox;
        char*                           m_pData;  // Contiguous buffer for GPU data ( vertices, extras, indices)
        mesh*                           m_pMesh;  // Separate allocations for CPU-persistent data
        lod*                            m_pLOD;
        submesh*                        m_pSubMesh;
        cluster*                        m_pCluster;
        xrsc::material_instance_ref*    m_pDefaultMaterialInstances;
        runtime_allocation              m_RunTimeSpace;
        std::size_t                     m_DataSize;
        std::size_t                     m_VertexOffset;
        std::size_t                     m_VertexExtrasOffset;
        std::size_t                     m_IndicesOffset;
        std::size_t                     m_ClusterDataOffset;
        std::uint16_t                   m_nMeshes;
        std::uint16_t                   m_nLODs;
        std::uint16_t                   m_nSubMeshs;
        std::uint16_t                   m_nClusters;
        std::uint32_t                   m_nIndices;
        std::uint32_t                   m_nVertices;
        std::uint16_t                   m_nDefaultMaterialInstances;
        gi_surfel*                      m_pGISurfel;            // m_nGISurfels fine ones, then m_nGIParents parents
        std::uint16_t*                  m_pGIVertSurfel;        // per vertex (the GPU buffer's order) its 4 nearest fine surfels, 0xFFFF: none
        gi_surfel_material*             m_pGISurfelMaterial;    // per fine surfel
        std::uint32_t                   m_nGISurfels;
        std::uint32_t                   m_nGIParents;
        gi_mode                         m_GIMode;
    };

    //-------------------------------------------------------------------------

    geom::geom(xserializer::stream& Steaming) noexcept
    {
        //xassert( Steaming.getResourceVersion() == xgeom::VERSION );
    }

    //-------------------------------------------------------------------------

    void geom::Initialize(void) noexcept
    {
        std::memset(this, 0, sizeof(*this));
    }

    //-------------------------------------------------------------------------
    void geom::Kill(void) noexcept
    {
        if (m_pMesh)                        delete[] m_pMesh;
        if (m_pLOD)                         delete[] m_pLOD;
        if (m_pSubMesh)                     delete[] m_pSubMesh;
        if (m_pCluster)                     delete[] m_pCluster;
        if (m_pDefaultMaterialInstances)    delete[] m_pDefaultMaterialInstances;
        if (m_pGISurfel)                    delete[] m_pGISurfel;
        if (m_pGIVertSurfel)                delete[] m_pGIVertSurfel;
        if (m_pGISurfelMaterial)            delete[] m_pGISurfelMaterial;
        if (m_pData)                       delete[] m_pData;

        Initialize();
    }

    //-------------------------------------------------------------------------

    int geom::findMeshIndex(const char* pName) const noexcept
    {
        auto meshes = getMeshes();
        for (auto i = 0u; i < meshes.size(); i++)
        {
            if (!std::strcmp(meshes[i].m_Name.data(), pName))
            {
                return i;
            }
        }
        return -1;
    }
}

//-------------------------------------------------------------------------
// serializer
//-------------------------------------------------------------------------
namespace xserializer::io_functions
{
    //-------------------------------------------------------------------------
    template<> inline
        xerr SerializeIO<xgeom_static::geom::lod>(xserializer::stream& Stream, const xgeom_static::geom::lod& Lod) noexcept
    {
        xerr Err;
        false
            || (Err = Stream.Serialize(Lod.m_ScreenArea))
            || (Err = Stream.Serialize(Lod.m_iSubmesh))
            || (Err = Stream.Serialize(Lod.m_nSubmesh))
            ;
        return Err;
    }

    //-------------------------------------------------------------------------
    template<> inline
    xerr SerializeIO<xgeom_static::geom::mesh>(xserializer::stream& Stream, const xgeom_static::geom::mesh& Mesh) noexcept
    {
        xerr Err;
        false
            || (Err = Stream.Serialize(Mesh.m_Name))
            || (Err = Stream.Serialize(Mesh.m_WorldPixelSize))
            || (Err = Stream.Serialize(Mesh.m_BBox.m_Min.m_X))
            || (Err = Stream.Serialize(Mesh.m_BBox.m_Min.m_Y))
            || (Err = Stream.Serialize(Mesh.m_BBox.m_Min.m_Z))
            || (Err = Stream.Serialize(Mesh.m_BBox.m_Max.m_X))
            || (Err = Stream.Serialize(Mesh.m_BBox.m_Max.m_Y))
            || (Err = Stream.Serialize(Mesh.m_BBox.m_Max.m_Z))
            || (Err = Stream.Serialize(Mesh.m_nLODs))
            || (Err = Stream.Serialize(Mesh.m_iLOD))
            ;
        return Err;
    }

    //-------------------------------------------------------------------------
    template<> inline
    xerr SerializeIO<xgeom_static::geom::submesh>(xserializer::stream& Stream, const xgeom_static::geom::submesh& Submesh) noexcept
    {
        xerr Err;
        false
            || (Err = Stream.Serialize(Submesh.m_iCluster))
            || (Err = Stream.Serialize(Submesh.m_nCluster))
            || (Err = Stream.Serialize(Submesh.m_iMaterial))
            ;
        return Err;
    }

    //-------------------------------------------------------------------------
    template<> inline
    xerr SerializeIO<xgeom_static::geom::cluster>(xserializer::stream& Stream, const xgeom_static::geom::cluster& Cluster) noexcept
    {
        xerr Err;
        false
            || (Err = Stream.Serialize(Cluster.m_nVertices))
            || (Err = Stream.Serialize(Cluster.m_nIndices))
            || (Err = Stream.Serialize(Cluster.m_iIndex))
            || (Err = Stream.Serialize(Cluster.m_iVertex))
            || (Err = Stream.Serialize(Cluster.m_BBox.m_Min.m_X))
            || (Err = Stream.Serialize(Cluster.m_BBox.m_Min.m_Y))
            || (Err = Stream.Serialize(Cluster.m_BBox.m_Min.m_Z))
            || (Err = Stream.Serialize(Cluster.m_BBox.m_Max.m_X))
            || (Err = Stream.Serialize(Cluster.m_BBox.m_Max.m_Y))
            || (Err = Stream.Serialize(Cluster.m_BBox.m_Max.m_Z))
            ;
        return Err;
    }

    //-------------------------------------------------------------------------
    // One definition for every geometry type: a host can include the loaders of more than one of them
    #ifndef XGEOM_MATERIAL_INSTANCE_REF_SERIALIZEIO
    #define XGEOM_MATERIAL_INSTANCE_REF_SERIALIZEIO
    template<> inline
    xerr SerializeIO<xrsc::material_instance_ref>(xserializer::stream& Stream, const xrsc::material_instance_ref& IR) noexcept
    {
        return Stream.Serialize(IR.m_Instance.m_Value);
    }
    #endif

    //-------------------------------------------------------------------------
    template<> inline
    xerr SerializeIO<xgeom_static::geom::gi_surfel>(xserializer::stream& Stream, const xgeom_static::geom::gi_surfel& S) noexcept
    {
        xerr Err;
        false
            || (Err = Stream.Serialize(S.m_Center.m_X))
            || (Err = Stream.Serialize(S.m_Center.m_Y))
            || (Err = Stream.Serialize(S.m_Center.m_Z))
            || (Err = Stream.Serialize(S.m_Radius))
            || (Err = Stream.Serialize(S.m_Normal.m_X))
            || (Err = Stream.Serialize(S.m_Normal.m_Y))
            || (Err = Stream.Serialize(S.m_Normal.m_Z))
            || (Err = Stream.Serialize(S.m_iParent))
            ;
        return Err;
    }

    //-------------------------------------------------------------------------
    template<> inline
    xerr SerializeIO<xgeom_static::geom::gi_surfel_material>(xserializer::stream& Stream, const xgeom_static::geom::gi_surfel_material& M) noexcept
    {
        xerr Err;
        false
            || (Err = Stream.Serialize(M.m_U))
            || (Err = Stream.Serialize(M.m_V))
            || (Err = Stream.Serialize(M.m_UVRadius))
            || (Err = Stream.Serialize(M.m_iMaterial))
            ;
        return Err;
    }

    //-------------------------------------------------------------------------
    template<> inline
    xerr SerializeIO<xgeom_static::geom>(xserializer::stream& Stream, const xgeom_static::geom& Geom) noexcept
    {
        xerr Err;
        false
            || (Err = Stream.Serialize(Geom.m_nMeshes))
            || (Err = Stream.Serialize(Geom.m_pMesh,                        Geom.m_nMeshes))
            || (Err = Stream.Serialize(Geom.m_nLODs))
            || (Err = Stream.Serialize(Geom.m_pLOD,                         Geom.m_nLODs))
            || (Err = Stream.Serialize(Geom.m_nSubMeshs))
            || (Err = Stream.Serialize(Geom.m_pSubMesh,                     Geom.m_nSubMeshs))
            || (Err = Stream.Serialize(Geom.m_nClusters))
            || (Err = Stream.Serialize(Geom.m_pCluster,                     Geom.m_nClusters))
            || (Err = Stream.Serialize(Geom.m_nDefaultMaterialInstances))
            || (Err = Stream.Serialize(Geom.m_pDefaultMaterialInstances,    Geom.m_nDefaultMaterialInstances))
            || (Err = Stream.Serialize(Geom.m_DataSize))
            || (Err = Stream.Serialize(Geom.m_pData,                        Geom.m_DataSize))
            || (Err = Stream.Serialize(Geom.m_RunTimeSpace))
            || (Err = Stream.Serialize(Geom.m_BBox.m_Min.m_X))
            || (Err = Stream.Serialize(Geom.m_BBox.m_Min.m_Y))
            || (Err = Stream.Serialize(Geom.m_BBox.m_Min.m_Z))
            || (Err = Stream.Serialize(Geom.m_BBox.m_Max.m_X))
            || (Err = Stream.Serialize(Geom.m_BBox.m_Max.m_Y))
            || (Err = Stream.Serialize(Geom.m_BBox.m_Max.m_Z))
            || (Err = Stream.Serialize(Geom.m_VertexOffset))
            || (Err = Stream.Serialize(Geom.m_VertexExtrasOffset))
            || (Err = Stream.Serialize(Geom.m_IndicesOffset))
            || (Err = Stream.Serialize(Geom.m_ClusterDataOffset))
            || (Err = Stream.Serialize(Geom.m_nVertices))
            || (Err = Stream.Serialize(Geom.m_nIndices))
            || (Err = Stream.Serialize(Geom.m_nGISurfels))
            || (Err = Stream.Serialize(Geom.m_nGIParents))
            || (Err = Stream.Serialize(Geom.m_pGISurfel,                    Geom.m_pGISurfel ? Geom.m_nGISurfels + Geom.m_nGIParents : 0u))
            || (Err = Stream.Serialize(Geom.m_pGIVertSurfel,                Geom.getGIVertexSurfels().size()))
            || (Err = Stream.Serialize(Geom.m_pGISurfelMaterial,            Geom.getGISurfelMaterials().size()))
            || (Err = Stream.Serialize(Geom.m_GIMode))
            ;
        return Err;
    }
}

#endif

