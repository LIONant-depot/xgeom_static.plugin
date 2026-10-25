#include "xgeom_static_compiler.h"
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif
#include <chrono>

#include <dependencies/xcontainer/source/xcontainer_lockless_pool.h>

#include "dependencies/xraw3d/source/xraw3d.h"
#include "dependencies/xraw3d/source/details/xraw3d_assimp_import_v3.h"

#include "dependencies/meshoptimizer/src/meshoptimizer.h"
#include "dependencies/xbitmap/source/xcolor.h"
#include "dependencies/xbits/source/xbits.h"

#include "../xgeom_static_descriptor.h"
#include "../xgeom_static.h"
#include "../xgeom_static_details.h"

#include "dependencies/xLIONRender/src/features/gi/xlionrender_surfel_gi.h"
#include "dependencies/xLIONRender/src/features/gi/xlionrender_surfel_gi_chunks.h"

#define STB_IMAGE_IMPLEMENTATION
#include "plugins/xmaterial_instance.plugin/source/Compiler/xmaterial_instance_import_compiler.h"

#include "dependencies/xproperty/source/xcore/my_properties.cpp"
#include "dependencies/xmath/source/bridge/xmath_to_xproperty.h"

#include <string>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <optional>
#include <iostream>

namespace xgeom_static_compiler
{
    struct implementation : xgeom_static_compiler::instance
    {
        using geom = xgeom_static::geom;

        struct vertex
        { 
            xmath::fvec3                    m_Position;
            std::array<xmath::fvec2, 4>     m_UVs;
            xcolori                         m_Color;
            xmath::fvec3                    m_Normal;
            xmath::fvec3                    m_Tangent;
            xmath::fvec3                    m_Binormal;
        };

        struct lod
        {
            float                           m_ScreenArea;
            std::vector<std::uint32_t>      m_Indices;
        };

        struct sub_mesh
        {
            std::vector<vertex>             m_Vertex;
            std::vector<std::uint32_t>      m_Indices;                      // Actual LOD 0 (Original mesh)
            std::vector<lod>                m_LODs;                         // This is LOD 1..n (New computed LODS)
            std::uint32_t                   m_iMaterial;
            int                             m_nWeights      { 0 };
            int                             m_nUVs          { 0 };
            bool                            m_bHasColor     { false };
            bool                            m_bHasUV1       { false };      // a second UV set (the optional UV1 stream)
            bool                            m_bHasNormal    { false };
            bool                            m_bHasBTN       { false };
        };

        struct mesh
        {
            std::string                     m_Name;
            std::vector<sub_mesh>           m_SubMesh;
        };

        //--------------------------------------------------------------------------------------

        implementation()
        {
            m_FinalGeom.Initialize();
        }

        //--------------------------------------------------------------------------------------

        xerr LoadRaw( const std::wstring_view Path )
        {
            xraw3d::assimp_v3::importer::settings Settings;
            xraw3d::assimp_v3::importer Importer;

            Settings.m_bStaticGeometry = true;
            Settings.m_pGeom = &m_RawGeom;
            Settings.m_pNode = &m_RootNode;
            Settings.m_pUnits = &m_Units;

            if ( auto Err = Importer.Import(Path, Settings); Err )
                return xerr::create_f<state, "Failed to import the asset">(Err);

            return {};
        }

        //--------------------------------------------------------------------------------------
/*
        void ConvertToCompilerMesh(void)
        {
            for( auto& Mesh : m_RawGeom.m_Mesh )
            {
                auto& NewMesh = m_CompilerMesh.emplace_back();
                NewMesh.m_Name = Mesh.m_Name;
            }

            std::vector<std::int32_t> GeomToCompilerVertMesh( m_RawGeom.m_Facet.size() * 3          );
            std::vector<std::int32_t> MaterialToSubmesh     ( m_RawGeom.m_MaterialInstance.size()   );

            int MinVert     = 0;
            int MaxVert     = int(GeomToCompilerVertMesh.size()-1);
            int CurMaterial = -1;
            int LastMesh    = -1;

            for( auto& Face : m_RawGeom.m_Facet )
            {
                auto& Mesh = m_CompilerMesh[Face.m_iMesh];

                // Make sure all faces are shorted by the mesh
                assert(Face.m_iMesh >= LastMesh);
                LastMesh = Face.m_iMesh;

                // are we dealing with a new mesh? if so we need to reset the remap of the submesh
                if( Mesh.m_SubMesh.size() == 0 )
                {
                    for (auto& R : MaterialToSubmesh) R = -1;
                }

                // are we dealiong with a new submesh is so we need to reset the verts
                if( MaterialToSubmesh[Face.m_iMaterialInstance] == -1 )
                {
                    MaterialToSubmesh[Face.m_iMaterialInstance] = int(Mesh.m_SubMesh.size());
                    auto& Submesh = Mesh.m_SubMesh.emplace_back();

                    Submesh.m_iMaterial = Face.m_iMaterialInstance;

                    for (int i = MinVert; i <= MaxVert; ++i)
                        GeomToCompilerVertMesh[i] = -1;

                    MaxVert = 0;
                    MinVert = int(GeomToCompilerVertMesh.size()-1);
                    CurMaterial = Face.m_iMaterialInstance;
                }
                else
                {
                    // Make sure that faces are shorted by materials
                    assert(CurMaterial >= Face.m_iMaterialInstance );
                }

                auto& SubMesh = Mesh.m_SubMesh[MaterialToSubmesh[Face.m_iMaterialInstance]];

                for( int i=0; i<3; ++i )
                {
                    if( GeomToCompilerVertMesh[Face.m_iVertex[i]] == -1 )
                    {
                        GeomToCompilerVertMesh[Face.m_iVertex[i]] = int(SubMesh.m_Vertex.size());
                        auto& CompilerVert = SubMesh.m_Vertex.emplace_back();
                        auto& RawVert      = m_RawGeom.m_Vertex[Face.m_iVertex[i]];

                        CompilerVert.m_Binormal = RawVert.m_BTN[0].m_Binormal;
                        CompilerVert.m_Tangent  = RawVert.m_BTN[0].m_Tangent;
                        CompilerVert.m_Normal   = RawVert.m_BTN[0].m_Normal;
                        CompilerVert.m_Color    = RawVert.m_Color[0];               // This could be n in the future...
                        CompilerVert.m_Position = RawVert.m_Position;
                        
                        if ( RawVert.m_nTangents ) SubMesh.m_bHasBTN    = true;
                        if ( RawVert.m_nNormals  ) SubMesh.m_bHasNormal = true;
                        if ( RawVert.m_nColors   ) SubMesh.m_bHasColor  = true;
                        if ( RawVert.m_nUVs > 1  ) SubMesh.m_bHasUV1    = true;

                        if( SubMesh.m_Indices.size() && SubMesh.m_nUVs != 0 && RawVert.m_nUVs < SubMesh.m_nUVs )
                        {
                            static bool bDontShowAgain = true;
                            if (bDontShowAgain)
                            {
                                bDontShowAgain = false;
                                printf("WARNING: Found a vertex with an inconsistent set of uvs (Expecting %d, found %d) MeshName: %s \n"
                                    , SubMesh.m_nUVs
                                    , RawVert.m_nUVs
                                    , Mesh.m_Name.data()
                                );
                            }
                        }
                        else
                        {
                            SubMesh.m_nUVs = RawVert.m_nUVs;
                            SubMesh.m_nUVs = 1; //HACK: Force it for now

                            for (int j = 0; j < RawVert.m_nUVs; ++j)
                                CompilerVert.m_UVs[j] = RawVert.m_UV[j];
                        }
                    }

                    assert( GeomToCompilerVertMesh[Face.m_iVertex[i]] >= 0 );
                    assert( GeomToCompilerVertMesh[Face.m_iVertex[i]] < m_RawGeom.m_Vertex.size() );
                    SubMesh.m_Indices.push_back(GeomToCompilerVertMesh[Face.m_iVertex[i]]);

                    MinVert = std::min( MinVert, (int)Face.m_iVertex[i] );
                    MaxVert = std::max( MaxVert, (int)Face.m_iVertex[i] );
                }
            }
        }
*/

void ConvertToCompilerMesh(void)
{
    for (auto& Mesh : m_RawGeom.m_Mesh)
    {
        auto& NewMesh = m_CompilerMesh.emplace_back();
        NewMesh.m_Name = Mesh.m_Name;
    }

    std::vector<std::int32_t> GeomToCompilerVertMesh(m_RawGeom.m_Vertex.size(), -1);
    std::vector<std::int32_t> MaterialToSubmesh(m_RawGeom.m_MaterialInstance.size(), -1);

    int CurMaterial = -1;
    int LastMesh = -1;

    for (auto& Face : m_RawGeom.m_Facet)
    {
        auto& Mesh = m_CompilerMesh[Face.m_iMesh];

        // Make sure all faces are sorted by the mesh
        assert(Face.m_iMesh >= LastMesh);
        LastMesh = Face.m_iMesh;

        // are we dealing with a new mesh? if so we need to reset the remap of the submesh
        if (Mesh.m_SubMesh.size() == 0)
        {
            std::fill(MaterialToSubmesh.begin(), MaterialToSubmesh.end(), -1);
        }

        // are we dealing with a new submesh? if so we need to reset the verts
        if (MaterialToSubmesh[Face.m_iMaterialInstance] == -1)
        {
            MaterialToSubmesh[Face.m_iMaterialInstance] = int(Mesh.m_SubMesh.size());
            auto& Submesh = Mesh.m_SubMesh.emplace_back();

            Submesh.m_iMaterial = Face.m_iMaterialInstance;

            std::fill(GeomToCompilerVertMesh.begin(), GeomToCompilerVertMesh.end(), -1);

            CurMaterial = Face.m_iMaterialInstance;
        }
        else
        {
            // Make sure that faces are sorted by materials
            assert(CurMaterial >= Face.m_iMaterialInstance);
        }

        auto& SubMesh = Mesh.m_SubMesh[MaterialToSubmesh[Face.m_iMaterialInstance]];

        for (int i = 0; i < 3; ++i)
        {
            if (GeomToCompilerVertMesh[Face.m_iVertex[i]] == -1)
            {
                GeomToCompilerVertMesh[Face.m_iVertex[i]] = int(SubMesh.m_Vertex.size());
                auto& CompilerVert = SubMesh.m_Vertex.emplace_back();
                auto& RawVert = m_RawGeom.m_Vertex[Face.m_iVertex[i]];

                CompilerVert.m_Binormal = RawVert.m_BTN[0].m_Binormal;
                CompilerVert.m_Tangent = RawVert.m_BTN[0].m_Tangent;
                CompilerVert.m_Normal = RawVert.m_BTN[0].m_Normal;
                CompilerVert.m_Color = RawVert.m_Color[0];               // This could be n in the future...
                CompilerVert.m_Position = RawVert.m_Position;

                if (RawVert.m_nTangents) SubMesh.m_bHasBTN = true;
                if (RawVert.m_nNormals) SubMesh.m_bHasNormal = true;
                if (RawVert.m_nColors) SubMesh.m_bHasColor = true;
                if (RawVert.m_nUVs > 1) SubMesh.m_bHasUV1 = true;

                if (SubMesh.m_Indices.size() && SubMesh.m_nUVs != 0 && RawVert.m_nUVs < SubMesh.m_nUVs)
                {
                    static bool bDontShowAgain = true;
                    if (bDontShowAgain)
                    {
                        bDontShowAgain = false;
                        printf("WARNING: Found a vertex with an inconsistent set of uvs (Expecting %d, found %d) MeshName: %s \n"
                            , SubMesh.m_nUVs
                            , RawVert.m_nUVs
                            , Mesh.m_Name.data()
                        );
                    }
                }
                else
                {
                    SubMesh.m_nUVs = RawVert.m_nUVs;

                    for (int j = 0; j < RawVert.m_nUVs; ++j)
                        CompilerVert.m_UVs[j] = RawVert.m_UV[j];
                }
            }

            assert(GeomToCompilerVertMesh[Face.m_iVertex[i]] >= 0);
            assert(GeomToCompilerVertMesh[Face.m_iVertex[i]] < m_RawGeom.m_Vertex.size());
            SubMesh.m_Indices.push_back(GeomToCompilerVertMesh[Face.m_iVertex[i]]);
        }
    }
}


        //--------------------------------------------------------------------------------------

        void GenenateLODs()
        {
            for (auto& M : m_CompilerMesh)
            {
                auto it = m_NameToDetail.find(M.m_Name);
                if (it == m_NameToDetail.end())
                    continue;

                auto* details = it->second;
                if (details->m_LODs.empty())
                    continue;

                for (auto& S : M.m_SubMesh)
                {
                    std::size_t IndexCount = S.m_Indices.size();

                    for (size_t i = 0; i < details->m_LODs.size(); ++i)
                    {
                        const std::size_t target_index_count = std::size_t(IndexCount * details->m_LODs[i].m_LODReduction + 0.005f) / 3 * 3;
                        const float target_error = 1e-2f;
                        const auto& Source = (S.m_LODs.size()) ? S.m_LODs.back().m_Indices : S.m_Indices;

                        if (Source.size() < target_index_count)
                            break;

                        auto& NewLod = S.m_LODs.emplace_back();

                        NewLod.m_Indices.resize(Source.size());
                        std::size_t new_size = meshopt_simplify(NewLod.m_Indices.data(), Source.data(), Source.size(), &S.m_Vertex[0].m_Position[0], S.m_Vertex.size(), sizeof(vertex), target_index_count, target_error);
                        NewLod.m_Indices.resize(new_size);

                        IndexCount = new_size;
                    }
                }
            }
        }

        //--------------------------------------------------------------------------------------

        static xmath::fvec2 oct_wrap(xmath::fvec2 v) noexcept
        {
            return (xmath::fvec2(1.0f) - xmath::fvec2(v.m_Y, v.m_X).Abs()) * (v.Step({ 0 }) * 2.0f - 1.0f);
        }

        //--------------------------------------------------------------------------------------

        static xmath::fvec2 oct_encode(xmath::fvec3 n) noexcept
        {
            n /= (xmath::Abs(n.m_X) + xmath::Abs(n.m_Y) + xmath::Abs(n.m_Z));
            xmath::fvec2 p = xmath::fvec2(n.m_X, n.m_Y);
            if (n.m_Z < 0.0f) p = oct_wrap(p);
            return p;
        }

        //--------------------------------------------------------------------------------------

        struct BBox3
        {
            xmath::fvec3 m_MinPos = xmath::fvec3(std::numeric_limits<float>::max());
            xmath::fvec3 m_MaxPos = xmath::fvec3(std::numeric_limits<float>::lowest());
            void Update(const xmath::fvec3& p) noexcept
            {
                m_MinPos = m_MinPos.Min(p);
                m_MaxPos = m_MaxPos.Max(p);
            }
            xmath::fbbox to_fbbox() const
            {
                xmath::fbbox bb;
                bb.m_Min = m_MinPos;
                bb.m_Max = m_MaxPos;
                return bb;
            }
        };

        //--------------------------------------------------------------------------------------

        struct BBox2
        {
            xmath::fvec2 m_MinUV = xmath::fvec2(std::numeric_limits<float>::max());
            xmath::fvec2 m_MaxUV = xmath::fvec2(std::numeric_limits<float>::lowest());
            void Update(const xmath::fvec2& uv)
            {
                m_MinUV = m_MinUV.Min(uv);
                m_MaxUV = m_MaxUV.Max(uv);
            }
        };

        //--------------------------------------------------------------------------------------
        // Cluster for recursive splitting (triangle ids)

        struct TriCluster
        {
            std::vector<uint32_t> tri_ids;
        };

        static xmath::fvec3 oct_decode(xmath::fvec2 e)
        {
            e = e * 2.0f - 1.0f;                          // [0,1] -> [-1,1]
            xmath::fvec3 v(e.m_X, e.m_Y, 1.0f - std::abs(e.m_X) - std::abs(e.m_Y));
            if (v.m_Z < 0.0f)
            {
                float temp_x = v.m_X;
                float temp_y = v.m_Y;
                v.m_X = (1.0f - std::abs(temp_y)) * (temp_x >= 0.0f ? 1.0f : -1.0f);
                v.m_Y = (1.0f - std::abs(temp_x)) * (temp_y >= 0.0f ? 1.0f : -1.0f);
            }
            return v.NormalizeSafe();
        }

        //--------------------------------------------------------------------------------------

        // One step of MakeClusters: c becomes a cluster when it fits the budget (MaxVerts) and its UVs span no more than MaxUVExtent, else its two
        // halves go on Pending (c2 under c1, so c1 and all it splits into come out first). The world size of c does not split it: each cluster's
        // positions are quantized relative to its own bounds (16 bits over its extent: a precision of extent / 65535, the 1 mm target up to 65.5 units and coarser past it), so a big
        // object is as many clusters as a small one with the same vertices - splitting by world extent made the clusters (the draws) grow with the area
        static void SplitCluster
        ( const std::vector<vertex>&        InputVerts
        , const std::vector<uint32_t>&      InputIndices
        , const TriCluster&                 c
        , uint32_t                          MaxVerts
        , const float                       MaxUVExtent
        , const std::vector<float>&         BinormalSigns
        , std::vector<geom::cluster>&       OutputClusters
        , std::vector<geom::vertex>&        AllStaticVerts
        , std::vector<geom::vertex_extras>& AllExtrasVerts
        , const bool                        HasColor
        , std::vector<std::uint32_t>&       AllColors
        , const bool                        HasUV1
        , std::vector<std::array<float, 2>>& AllUV1
        , std::vector<geom::cluster_data>&  AllClusterData
        , std::vector<uint32_t>&            AllIndices
        , std::vector<TriCluster>&          Pending
        )
        {
            if (c.tri_ids.empty()) return;
            BBox3 bb_pos;
            BBox2 bb_uv;

            for (uint32_t ti : c.tri_ids)
            {
                for (int j = 0; j < 3; ++j)
                {
                    uint32_t vi = InputIndices[ti * 3 + j];
                    bb_pos.Update(InputVerts[vi].m_Position);
                    bb_uv.Update(InputVerts[vi].m_UVs[0]);
                }
            }

            xmath::fvec3    extent_pos      = bb_pos.m_MaxPos - bb_pos.m_MinPos;
            xmath::fvec2    extent_uv       = bb_uv.m_MaxUV - bb_uv.m_MinUV;
            float           max_e_uv        = std::max(extent_uv.m_X, extent_uv.m_Y);
            bool            small_extent    = (max_e_uv <= MaxUVExtent);       // the UVs: 16 bits over the cluster's UV span (texture space, not world size)

            // A single triangle can not be split: when its UVs span more than MaxUVExtent it is still one cluster, its UVs just quantized coarser
            const bool      single_tri      = (c.tri_ids.size() == 1);

            std::unordered_set<uint32_t> used_verts;
            if (small_extent || single_tri)
            {
                for (uint32_t ti : c.tri_ids)
                {
                    for (int j = 0; j < 3; ++j)
                    {
                        used_verts.insert(InputIndices[ti * 3 + j]);
                    }
                }
            }

            if ((small_extent && used_verts.size() <= MaxVerts) || single_tri)
            {
                // Remap verts locally
                std::vector<uint32_t> new_vert_ids(used_verts.begin(), used_verts.end());
                std::sort(new_vert_ids.begin(), new_vert_ids.end());
                std::unordered_map<uint32_t, uint32_t> old_to_new = {};
                uint32_t new_id = 0;
                for (uint32_t ov : new_vert_ids)
                {
                    old_to_new[ov] = new_id++;
                }

                // Use precomputed bboxes
                xmath::fvec3 pos_min    = bb_pos.m_MinPos;
                xmath::fvec3 pos_max    = bb_pos.m_MaxPos;
                xmath::fvec3 pos_center = (pos_min + pos_max) * 0.5f;
                xmath::fvec3 pos_scale  = xmath::fvec3::Max((pos_max - pos_min) * 0.5f, xmath::fvec3(1e-6f));
                xmath::fvec2 uv_min     = bb_uv.m_MinUV;
                xmath::fvec2 uv_max     = bb_uv.m_MaxUV;
                xmath::fvec2 uv_scale   = xmath::fvec2::Max(uv_max - uv_min, xmath::fvec2(1e-6f));

                // Build local indices
                std::vector<unsigned int> local_indices;
                local_indices.reserve(c.tri_ids.size() * 3);
                for (uint32_t ti : c.tri_ids)
                {
                    for (int j = 0; j < 3; ++j)
                    {
                        uint32_t ov = InputIndices[ti * 3 + j];
                        local_indices.push_back(old_to_new[ov]);
                    }
                }

                // Optimize vertex cache
                meshopt_optimizeVertexCache(local_indices.data(), local_indices.data(), local_indices.size(), static_cast<unsigned int>(new_vert_ids.size()));

                // Prepare positions for overdraw optimization
                std::vector<float> local_positions(new_vert_ids.size() * 3);
                for (uint32_t i = 0; i < new_vert_ids.size(); ++i)
                {
                    const auto& pos = InputVerts[new_vert_ids[i]].m_Position;
                    local_positions[i * 3 + 0] = pos.m_X;
                    local_positions[i * 3 + 1] = pos.m_Y;
                    local_positions[i * 3 + 2] = pos.m_Z;
                }

                // Optimize overdraw
                meshopt_optimizeOverdraw(local_indices.data(), local_indices.data(), local_indices.size(), local_positions.data(), static_cast<unsigned int>(new_vert_ids.size()), sizeof(float) * 3, 1.05f);

                // Generate fetch remap
                std::vector<unsigned int> fetch_remap(new_vert_ids.size());
                meshopt_optimizeVertexFetchRemap(fetch_remap.data(), local_indices.data(), local_indices.size(), static_cast<unsigned int>(new_vert_ids.size()));

                // Pack original compressed vertices and extras
                std::vector<geom::vertex>           original_static(new_vert_ids.size());
                std::vector<geom::vertex_extras>    original_extras(new_vert_ids.size());
                std::vector<std::uint32_t>          original_colors(new_vert_ids.size());
                std::vector<std::array<float, 2>>   original_uv1(new_vert_ids.size());
                for (uint32_t i = 0; i < new_vert_ids.size(); ++i)
                {
                    const uint32_t  ov = new_vert_ids[i];
                    const vertex& v = InputVerts[ov];

                    // Color: RGBA8 as imported (sRGB), white for a submesh without colors when another one has them
                    original_colors[i] = HasColor ? v.m_Color.m_Value : 0xFFFFFFFFu;
                    // UV1: the second UV set, UV0 for a submesh without one when another one has it (what the shader reads without the stream)
                    const auto& uv1 = v.m_UVs[HasUV1 ? 1 : 0];
                    original_uv1[i] = { uv1.m_X, uv1.m_Y };
                    const float     sign_val = BinormalSigns[ov];
                    const uint32_t  sign_bit = (sign_val < 0.0f ? 1u : 0u);

                    // Pos compression
                    const auto pos = ((v.m_Position - pos_center) / pos_scale + 1.0f) * 32767.5f - 32768.0f;
                    original_static[i].m_XPos = static_cast<int16_t>(std::round(pos.m_X));
                    original_static[i].m_YPos = static_cast<int16_t>(std::round(pos.m_Y));
                    original_static[i].m_ZPos = static_cast<int16_t>(std::round(pos.m_Z));

                    // UV
                    const auto norm_uv = (v.m_UVs[0] - uv_min) / uv_scale;
                    original_extras[i].m_UV[0] = static_cast<uint16_t>(std::round(norm_uv.m_X * 65535.0f));
                    original_extras[i].m_UV[1] = static_cast<uint16_t>(std::round(norm_uv.m_Y * 65535.0f));

                    // Oct normal (12 bits each)
                    const auto      oct_n   = oct_encode(v.m_Normal.NormalizeSafeCopy());
                    const uint32_t  n_x     = static_cast<uint32_t>(std::round((oct_n.m_X * 0.5f + 0.5f) * 4095.0f));
                    const uint32_t  n_y     = static_cast<uint32_t>(std::round((oct_n.m_Y * 0.5f + 0.5f) * 4095.0f));

                    // Oct tangent (12/11 bits)
                    const auto      oct_t   = oct_encode(v.m_Tangent.NormalizeSafeCopy());
                    const uint32_t  t_x     = static_cast<uint32_t>(std::round((oct_t.m_X * 0.5f + 0.5f) * 4095.0f));
                    const uint32_t  t_y     = static_cast<uint32_t>(std::round((oct_t.m_Y * 0.5f + 0.5f) * 2047.0f));

                    // High bits (assuming m_OctNormal is uint8_t[4]: [0]=N_x high, [1]=N_y high, [0]=T_x high, [1]=T_y high)
                    original_extras[i].m_OctNormal[0]  = static_cast<uint8_t>(n_x >> 4);
                    original_extras[i].m_OctNormal[1]  = static_cast<uint8_t>(n_y >> 4);
                    original_extras[i].m_OctTangent[0] = static_cast<uint8_t>(t_x >> 4);
                    original_extras[i].m_OctTangent[1] = static_cast<uint8_t>(t_y >> 3);

                    // Low bits + sign to extra (uint16_t)
                    uint16_t extra_bits = 0u;
                    extra_bits |= ((n_x & 0xFu) << 0);         // 0-3: N_x low
                    extra_bits |= ((n_y & 0xFu) << 4);         // 4-7: N_y low
                    extra_bits |= ((t_x & 0xFu) << 8);         // 8-11: T_x low
                    extra_bits |= ((t_y & 0x7u) << 12);        // 12-14: T_y low
                    extra_bits |= (sign_bit << 15);            // 15: sign
                    original_static[i].m_Extra = extra_bits;

                    //SANITY CHECK: Decode normal (shader-equivalent)
                    if (false)
                    {
                        uint32_t high_nx = static_cast<uint32_t>(original_extras[i].m_OctNormal[0]) << 4;
                        uint32_t high_ny = static_cast<uint32_t>(original_extras[i].m_OctNormal[1]) << 4;
                        uint32_t low_nx = (extra_bits >> 0) & 0xFu;
                        uint32_t low_ny = (extra_bits >> 4) & 0xFu;
                        uint32_t combined_nx = high_nx | low_nx;
                        uint32_t combined_ny = high_ny | low_ny;
                        xmath::fvec2 enc_normal(static_cast<float>(combined_nx) / 4095.0f, static_cast<float>(combined_ny) / 4095.0f);
                        xmath::fvec3 decoded_normal = oct_decode(enc_normal);  

                        xmath::fvec3 orig_normal = v.m_Normal.NormalizeSafeCopy();
                        float error = (decoded_normal - orig_normal).Length();
                        assert(error < 0.01f);
                    }
                }

                // Remap vertices and extras
                std::vector<geom::vertex> remapped_static(new_vert_ids.size());
                meshopt_remapVertexBuffer(remapped_static.data(), original_static.data(), new_vert_ids.size(), sizeof(geom::vertex), fetch_remap.data());

                std::vector<geom::vertex_extras> remapped_extras(new_vert_ids.size());
                meshopt_remapVertexBuffer(remapped_extras.data(), original_extras.data(), new_vert_ids.size(), sizeof(geom::vertex_extras), fetch_remap.data());

                std::vector<std::uint32_t> remapped_colors(new_vert_ids.size());
                meshopt_remapVertexBuffer(remapped_colors.data(), original_colors.data(), new_vert_ids.size(), sizeof(std::uint32_t), fetch_remap.data());

                std::vector<std::array<float, 2>> remapped_uv1(new_vert_ids.size());
                meshopt_remapVertexBuffer(remapped_uv1.data(), original_uv1.data(), new_vert_ids.size(), sizeof(remapped_uv1[0]), fetch_remap.data());

                // Remap indices
                meshopt_remapIndexBuffer(local_indices.data(), local_indices.data(), local_indices.size(), fetch_remap.data());

                // Append verts to global
                uint32_t cluster_vert_start = static_cast<uint32_t>(AllStaticVerts.size());
                AllStaticVerts.insert(AllStaticVerts.end(), remapped_static.begin(), remapped_static.end());
                AllExtrasVerts.insert(AllExtrasVerts.end(), remapped_extras.begin(), remapped_extras.end());
                AllColors.insert(AllColors.end(), remapped_colors.begin(), remapped_colors.end());
                AllUV1.insert(AllUV1.end(), remapped_uv1.begin(), remapped_uv1.end());

                // Append indices to global
                uint32_t cluster_index_start = static_cast<uint32_t>(AllIndices.size());
                for (auto idx : local_indices)
                {
                    AllIndices.push_back(idx);
                }

                // Create cluster
                geom::cluster_data  cd;
                cd.m_PosScaleAndWPADDING.m_X        = pos_scale.m_X;
                cd.m_PosScaleAndWPADDING.m_Y        = pos_scale.m_Y;
                cd.m_PosScaleAndWPADDING.m_Z        = pos_scale.m_Z;
                cd.m_PosTrasnlationAndWPADDING.m_X  = pos_center.m_X;
                cd.m_PosTrasnlationAndWPADDING.m_Y  = pos_center.m_Y;
                cd.m_PosTrasnlationAndWPADDING.m_Z  = pos_center.m_Z;
                cd.m_UVScaleTranslation.m_X         = uv_scale.m_X;
                cd.m_UVScaleTranslation.m_Y         = uv_scale.m_Y;
                cd.m_UVScaleTranslation.m_Z         = uv_min.m_X;
                cd.m_UVScaleTranslation.m_W         = uv_min.m_Y;
                AllClusterData.push_back(cd);

                geom::cluster       cl;
                cl.m_BBox                           = bb_pos.to_fbbox();
                cl.m_iIndex                         = cluster_index_start;
                cl.m_nIndices                       = static_cast<uint32_t>(c.tri_ids.size() * 3);
                cl.m_iVertex                        = cluster_vert_start;
                cl.m_nVertices                      = static_cast<uint32_t>(new_vert_ids.size());
                OutputClusters.push_back(cl);
            }
            else
            {
                // Choose split axis based on max extent
                float   maxes[5]    = { extent_pos.m_X, extent_pos.m_Y, extent_pos.m_Z, extent_uv.m_X, extent_uv.m_Y };
                int     axis        = 0;
                float   max_val     = maxes[0];
                for (int i = 1; i < 5; ++i)
                {
                    if (maxes[i] > max_val)
                    {
                        max_val = maxes[i];
                        axis = i;
                    }
                }

                float   split_pos;
                bool    is_pos_axis = (axis < 3);
                int     sub_axis    = is_pos_axis ? axis : (axis - 3);
                if (is_pos_axis)
                {
                    split_pos = (bb_pos.m_MinPos[sub_axis] + bb_pos.m_MaxPos[sub_axis]) * 0.5f;
                }
                else
                {
                    split_pos = (bb_uv.m_MinUV[sub_axis] + bb_uv.m_MaxUV[sub_axis]) * 0.5f;
                }

                // The centroid of a triangle on the split axis
                const auto Centroid = [&](uint32_t ti) -> float
                {
                    uint32_t i0 = InputIndices[ti * 3 + 0];
                    uint32_t i1 = InputIndices[ti * 3 + 1];
                    uint32_t i2 = InputIndices[ti * 3 + 2];

                    if (is_pos_axis)
                    {
                        xmath::fvec3 cent_pos = (InputVerts[i0].m_Position + InputVerts[i1].m_Position + InputVerts[i2].m_Position) / 3.0f;
                        return cent_pos[sub_axis];
                    }
                    else
                    {
                        xmath::fvec2 cent_uv = (InputVerts[i0].m_UVs[0] + InputVerts[i1].m_UVs[0] + InputVerts[i2].m_UVs[0]) / 3.0f;
                        return cent_uv[sub_axis];
                    }
                };

                TriCluster c1, c2;
                for (uint32_t ti : c.tri_ids)
                {
                    if (Centroid(ti) < split_pos)   c1.tri_ids.push_back(ti);
                    else                            c2.tri_ids.push_back(ti);
                }

                // The middle of the bounds can leave every centroid on one side (big triangles, a fan out of one corner):
                // that half is c again and splitting it would never end. Then split at the median centroid, which always gives two smaller halves
                if (c1.tri_ids.empty() || c2.tri_ids.empty())
                {
                    c1.tri_ids = c.tri_ids;
                    c2.tri_ids.clear();

                    const auto Mid = c1.tri_ids.begin() + c1.tri_ids.size() / 2;
                    std::nth_element(c1.tri_ids.begin(), Mid, c1.tri_ids.end(), [&](uint32_t a, uint32_t b)
                    {
                        const float ca = Centroid(a), cb = Centroid(b);
                        return ca < cb || (ca == cb && a < b);
                    });
                    c2.tri_ids.assign(Mid, c1.tri_ids.end());
                    c1.tri_ids.erase(Mid, c1.tri_ids.end());
                }

                Pending.push_back(std::move(c2));
                Pending.push_back(std::move(c1));
            }
        }

        //--------------------------------------------------------------------------------------
        // Splits the triangles of Root into clusters, depth first (c1 before c2). The halves wait on a heap stack: how deep the split goes
        // depends on the mesh, and every level holds a cluster and its sets, so a call per level could overflow the thread stack

        static void MakeClusters
        ( const std::vector<vertex>&        InputVerts
        , const std::vector<uint32_t>&      InputIndices
        , const TriCluster&                 Root
        , uint32_t                          MaxVerts
        , const float                       MaxUVExtent
        , const std::vector<float>&         BinormalSigns
        , std::vector<geom::cluster>&       OutputClusters
        , std::vector<geom::vertex>&        AllStaticVerts
        , std::vector<geom::vertex_extras>& AllExtrasVerts
        , const bool                        HasColor
        , std::vector<std::uint32_t>&       AllColors
        , const bool                        HasUV1
        , std::vector<std::array<float, 2>>& AllUV1
        , std::vector<geom::cluster_data>&  AllClusterData
        , std::vector<uint32_t>&            AllIndices
        )
        {
            std::vector<TriCluster> Pending;
            Pending.push_back(Root);
            while (Pending.empty() == false)
            {
                const TriCluster c = std::move(Pending.back());
                Pending.pop_back();
                SplitCluster(InputVerts, InputIndices, c, MaxVerts, MaxUVExtent, BinormalSigns, OutputClusters, AllStaticVerts, AllExtrasVerts, HasColor, AllColors, HasUV1, AllUV1, AllClusterData, AllIndices, Pending);
            }
        }

        //--------------------------------------------------------------------------------------

        void ConvertToGeom(float target_precision)
        {
            const std::vector<mesh>&            compiler_meshes = m_CompilerMesh;
            geom&                               result          = m_FinalGeom;
            std::vector<geom::mesh>             OutMeshes;
            std::vector<geom::lod>              OutLODs;
            std::vector<geom::submesh>          OutSubmeshes;
            std::vector<geom::cluster>          OutClusters;
            std::vector<geom::vertex>           OutAllStaticVerts;
            std::vector<geom::vertex_extras>    OutAllExtrasVerts;
            std::vector<std::uint32_t>          OutAllColors;
            std::vector<std::array<float, 2>>   OutAllUV1;
            std::vector<geom::cluster_data>     OutClusterData;
            std::vector<uint32_t>               OutAllIndices;
            BBox3                               OutGlobalBBox;
            std::uint16_t                       current_lod_idx         = 0;
            std::uint16_t                       current_submesh_idx     = 0;
            std::uint16_t                       current_cluster_idx     = 0;
            float                               max_uv_extent           = target_precision * 65535.0f;     // UV units: 16 bits at target_precision

            for (const auto& input_mesh : compiler_meshes)
            {
                BBox3       mesh_bb         = {};
                float       total_edge_len  = 0.0f;
                uint32_t    num_edges       = 0;
                for (const auto& input_sm : input_mesh.m_SubMesh)
                {
                    for (const auto& v : input_sm.m_Vertex)
                    {
                        mesh_bb.Update(v.m_Position);
                        OutGlobalBBox.Update(v.m_Position);
                    }

                    for (size_t ti = 0; ti < input_sm.m_Indices.size() / 3; ++ti)
                    {
                        std::uint32_t i1 = input_sm.m_Indices[ti * 3 + 0];
                        std::uint32_t i2 = input_sm.m_Indices[ti * 3 + 1];
                        std::uint32_t i3 = input_sm.m_Indices[ti * 3 + 2];
                        total_edge_len  += (input_sm.m_Vertex[i1].m_Position - input_sm.m_Vertex[i2].m_Position).Length();
                        total_edge_len  += (input_sm.m_Vertex[i2].m_Position - input_sm.m_Vertex[i3].m_Position).Length();
                        total_edge_len  += (input_sm.m_Vertex[i3].m_Position - input_sm.m_Vertex[i1].m_Position).Length();
                        num_edges       += 3;
                    }

                    for (const auto& lod_in : input_sm.m_LODs)
                    {
                        for (size_t ti = 0; ti < lod_in.m_Indices.size() / 3; ++ti)
                        {
                            std::uint32_t i1 = lod_in.m_Indices[ti * 3 + 0];
                            std::uint32_t i2 = lod_in.m_Indices[ti * 3 + 1];
                            std::uint32_t i3 = lod_in.m_Indices[ti * 3 + 2];
                            total_edge_len  += (input_sm.m_Vertex[i1].m_Position - input_sm.m_Vertex[i2].m_Position).Length();
                            total_edge_len  += (input_sm.m_Vertex[i2].m_Position - input_sm.m_Vertex[i3].m_Position).Length();
                            total_edge_len  += (input_sm.m_Vertex[i3].m_Position - input_sm.m_Vertex[i1].m_Position).Length();
                            num_edges       += 3;
                        }
                    }
                }

                geom::mesh out_m;
                xstrtool::Copy(out_m.m_Name, input_mesh.m_Name);
                out_m.m_Name[31]        = '\0';
                out_m.m_WorldPixelSize  = (num_edges > 0) ? total_edge_len / num_edges : 0.0f;
                out_m.m_BBox            = mesh_bb.to_fbbox();
                out_m.m_nLODs           = static_cast<uint16_t>(input_mesh.m_SubMesh.empty() ? 1 : input_mesh.m_SubMesh[0].m_LODs.size() + 1);
                out_m.m_iLOD            = current_lod_idx;
                OutMeshes.push_back(out_m);

                current_lod_idx += out_m.m_nLODs;
                for (size_t lod_level = 0; lod_level < out_m.m_nLODs; ++lod_level)
                {
                    geom::lod out_l;
                    out_l.m_ScreenArea  = (lod_level == 0) ? 1.0f : (input_mesh.m_SubMesh.empty() ? 0.0f : input_mesh.m_SubMesh[0].m_LODs[lod_level - 1].m_ScreenArea);
                    out_l.m_iSubmesh    = current_submesh_idx;
                    out_l.m_nSubmesh    = static_cast<uint16_t>(input_mesh.m_SubMesh.size());
                    OutLODs.push_back(out_l);

                    current_submesh_idx += out_l.m_nSubmesh;
                    for (const auto& input_sm : input_mesh.m_SubMesh)
                    {
                        geom::submesh out_sm;
                        out_sm.m_iMaterial  = static_cast<uint16_t>(input_sm.m_iMaterial);
                        out_sm.m_iCluster   = current_cluster_idx;

                        const std::vector<uint32_t>&  lod_indices     = (lod_level == 0) ? input_sm.m_Indices : ((lod_level - 1 < input_sm.m_LODs.size()) ? input_sm.m_LODs[lod_level - 1].m_Indices : input_sm.m_Indices);
                        auto                          binormal_signs  = std::vector<float>(input_sm.m_Vertex.size(), 1.0f);
                        for (size_t i = 0; i < input_sm.m_Vertex.size(); ++i)
                        {
                            const vertex& v = input_sm.m_Vertex[i];
                            if (input_sm.m_bHasBTN)
                            {
                                xmath::fvec3    computed_binormal   = xmath::fvec3::Cross(v.m_Normal, v.m_Tangent);
                                float           dot_val             = xmath::fvec3::Dot(computed_binormal, v.m_Binormal);
                                binormal_signs[i] = (dot_val >= 0.0f) ? 1.0f : -1.0f;
                            }
                        }
                        TriCluster  initial  = {};
                        uint32_t    num_tris = static_cast<uint32_t>(lod_indices.size() / 3);

                        initial.tri_ids.resize(num_tris);
                        for (uint32_t i = 0; i < num_tris; ++i) initial.tri_ids[i] = i;

                        size_t prev_num_clusters = OutClusters.size();
                        if (m_nGIRegions > 1)      // GI sections: no cluster crosses a region (a section is whole clusters: BakeGISections)
                        {
                            std::vector<TriCluster> ByRegion(static_cast<std::size_t>(m_nGIRegions));
                            for (const std::uint32_t t : initial.tri_ids)
                            {
                                const auto& A = input_sm.m_Vertex[lod_indices[3 * t]].m_Position; const auto& B = input_sm.m_Vertex[lod_indices[3 * t + 1]].m_Position; const auto& C = input_sm.m_Vertex[lod_indices[3 * t + 2]].m_Position;
                                ByRegion[std::size_t(GIRegionOf((A + B + C) * (1.0f / 3.0f)))].tri_ids.push_back(t);
                            }
                            for (std::size_t g = 0; g < ByRegion.size(); ++g)
                            {
                                if (ByRegion[g].tri_ids.empty()) continue;
                                const std::size_t Before = OutClusters.size();
                                MakeClusters(input_sm.m_Vertex, lod_indices, ByRegion[g], 65534, max_uv_extent, binormal_signs, OutClusters, OutAllStaticVerts, OutAllExtrasVerts, input_sm.m_bHasColor, OutAllColors, input_sm.m_bHasUV1, OutAllUV1, OutClusterData, OutAllIndices);
                                m_ClusterRegion.resize(OutClusters.size(), -1);
                                for (std::size_t c = Before; c < OutClusters.size(); ++c) m_ClusterRegion[c] = int(g);
                            }
                        }
                        else MakeClusters(input_sm.m_Vertex, lod_indices, initial, 65534, max_uv_extent, binormal_signs, OutClusters, OutAllStaticVerts, OutAllExtrasVerts, input_sm.m_bHasColor, OutAllColors, input_sm.m_bHasUV1, OutAllUV1, OutClusterData, OutAllIndices);

                        out_sm.m_nCluster    = static_cast<uint16_t>(OutClusters.size() - prev_num_clusters);
                        current_cluster_idx += out_sm.m_nCluster;
                        OutSubmeshes.push_back(out_sm);
                    }
                }
            }
            result.m_nMeshes    = static_cast<std::uint16_t>(OutMeshes.size());
            result.m_pMesh      = new geom::mesh[result.m_nMeshes];
            std::ranges::copy(OutMeshes, result.m_pMesh);
            result.m_nLODs      = static_cast<std::uint16_t>(OutLODs.size());
            result.m_pLOD       = new geom::lod[result.m_nLODs];
            std::ranges::copy(OutLODs, result.m_pLOD);
            result.m_nSubMeshs  = static_cast<std::uint16_t>(OutSubmeshes.size());
            result.m_pSubMesh   = new geom::submesh[result.m_nSubMeshs];
            std::ranges::copy(OutSubmeshes, result.m_pSubMesh);
            result.m_nClusters  = static_cast<std::uint16_t>(OutClusters.size());
            result.m_pCluster   = new geom::cluster[result.m_nClusters];
            std::ranges::copy(OutClusters, result.m_pCluster);
            result.m_BBox       = OutGlobalBBox.to_fbbox();
            result.m_nVertices  = static_cast<std::uint32_t>(OutAllStaticVerts.size());
            result.m_nIndices   = static_cast<std::uint32_t>(OutAllIndices.size());

            //
            // Set all the material instances
            //
            result.m_nDefaultMaterialInstances = static_cast<std::uint16_t>(m_RawGeom.m_MaterialInstance.size());
            result.m_pDefaultMaterialInstances = new xrsc::material_instance_ref[result.m_nDefaultMaterialInstances];

            // Set default values
            std::memset(result.m_pDefaultMaterialInstances, 0, result.m_nDefaultMaterialInstances * sizeof(*result.m_pDefaultMaterialInstances));

            // Set all the materials that we know about
            for (auto& E : m_RawGeom.m_MaterialInstance)
            {
                const auto Index = static_cast<int>(&E - m_RawGeom.m_MaterialInstance.data());
                if (int iMaterial = m_Descriptor.findMaterial(E.m_Name); iMaterial != -1)
                    result.m_pDefaultMaterialInstances[Index] = m_Descriptor.m_MaterialInstRefList[iMaterial];
            }

            //
            // Build the final data
            //
            // Compute aligned sizes for GPU data
            auto align = [](std::size_t offset, std::size_t alignment) constexpr -> std::size_t
            {
                return (offset + alignment - 1) & ~(alignment - 1);
            };

            constexpr std::size_t   vulkan_align    = 64; // Min for Vulkan buffers/UBO
            const std::size_t       VertexSize      = OutAllStaticVerts.size() * sizeof(geom::vertex);
            const std::size_t       ExtrasSize      = OutAllExtrasVerts.size() * sizeof(geom::vertex_extras);
            const std::size_t       IndicesSize     = OutAllIndices.size()     * sizeof(std::uint16_t);
            const std::size_t       ClusterDataSize = OutClusterData.size()    * sizeof(geom::cluster_data);

            // The optional streams: only the ones some submesh has
            const bool              bHasColor       = std::ranges::any_of(compiler_meshes, [](const mesh& M) { return std::ranges::any_of(M.m_SubMesh, [](const auto& S) { return S.m_bHasColor; }); });
            const std::size_t       ColorSize       = bHasColor ? OutAllColors.size() * sizeof(std::uint32_t) : 0;
            assert(OutAllColors.size() == OutAllStaticVerts.size());
            const bool              bHasUV1         = std::ranges::any_of(compiler_meshes, [](const mesh& M) { return std::ranges::any_of(M.m_SubMesh, [](const auto& S) { return S.m_bHasUV1; }); });
            const std::size_t       UV1Size         = bHasUV1 ? OutAllUV1.size() * sizeof(OutAllUV1[0]) : 0;
            assert(OutAllUV1.size() == OutAllStaticVerts.size());

            std::size_t             current_offset  = 0;

            result.m_VertexOffset           = current_offset; current_offset = align(current_offset + VertexSize,       vulkan_align);
            result.m_VertexExtrasOffset     = current_offset; current_offset = align(current_offset + ExtrasSize,       vulkan_align);
            result.m_IndicesOffset          = current_offset; current_offset = align(current_offset + IndicesSize,      vulkan_align);
            result.m_ClusterDataOffset      = current_offset; current_offset = align(current_offset + ClusterDataSize,  vulkan_align);
            result.m_VertexColorOffset      = current_offset; current_offset = align(current_offset + ColorSize,        vulkan_align);
            result.m_UV1Offset              = current_offset; current_offset = align(current_offset + UV1Size,          vulkan_align);
            result.m_OptionalStreams        = std::uint8_t((bHasColor ? geom::stream_color0_v : 0) | (bHasUV1 ? geom::stream_uv1_v : 0));
            result.m_DataSize               = current_offset;
            result.m_pData                  = new char[result.m_DataSize];

            // Copy data into m_pData
            std::memcpy(result.m_pData + result.m_VertexOffset,         OutAllStaticVerts.data(), VertexSize);
            std::memcpy(result.m_pData + result.m_VertexExtrasOffset,   OutAllExtrasVerts.data(), ExtrasSize);
            std::memcpy(result.m_pData + result.m_ClusterDataOffset,    OutClusterData.data(),    ClusterDataSize);
            std::memcpy(result.m_pData + result.m_VertexColorOffset,    OutAllColors.data(),      ColorSize);
            std::memcpy(result.m_pData + result.m_UV1Offset,            OutAllUV1.data(),         UV1Size);

            // Copy the indices
            auto pIndex = reinterpret_cast<std::uint16_t*>(result.m_pData + result.m_IndicesOffset);
            for (size_t i = 0; i < OutAllIndices.size(); ++i)
            {
                assert(OutAllIndices[i] < 0xffff);
                pIndex[i] = static_cast<std::uint16_t>(OutAllIndices[i]);
            }

            // Make sure that at least we have one cluster
            assert(result.m_nClusters >= 1);
        }

        //--------------------------------------------------------------------------------------

        static void MergeMeshes(std::unordered_map<std::string, xgeom_static::mesh_details*>& OutHashMap, xraw3d::geom& Geom, const xgeom_static::descriptor& Descriptor, const xgeom_static::details& Details) noexcept
        {
            if (Descriptor.m_bMergeAllMeshes)
            {
                std::string newName = Descriptor.m_AllMeshesDetails.m_Name;
                Geom.CollapseMeshes(newName);
                if (!Geom.m_Mesh.empty())
                {
                    Geom.m_Mesh[0].m_Name = newName;
                }
                OutHashMap[newName] = const_cast<xgeom_static::mesh_details*>(&Descriptor.m_AllMeshesDetails);
            }
            else
            {
                for (const auto& group : Descriptor.m_MergeGroupList)
                {
                    std::string newName = group.m_MeshDetails.m_Name;

                    std::unordered_set<int> olds;
                    for (const auto& np : group.m_NodePathList)
                    {
                        std::string prefix = np + "/";
                        for (int i = 0; i < static_cast<int>(Geom.m_Mesh.size()); ++i)
                        {
                            if (Geom.m_Mesh[i].m_ScenePath.starts_with(prefix))
                                olds.insert(i);
                        }
                    }

                    if (olds.empty()) continue;

                    int survivor = *std::min_element(olds.begin(), olds.end());

                    int maxBones = 0;
                    for (int i : olds)
                        maxBones = std::max(maxBones, Geom.m_Mesh[i].m_nBones);
                    Geom.m_Mesh[survivor].m_nBones = maxBones;

                    Geom.m_Mesh[survivor].m_Name = newName;

                    for (int i : olds)
                    {
                        if (i == survivor) continue;
                        for (auto& f : Geom.m_Facet)
                            if (f.m_iMesh == i)
                                f.m_iMesh = survivor;
                    }

                    // Delete non-survivors for this group
                    std::vector<int> groupDelete;
                    for (int i : olds)
                        if (i != survivor)
                            groupDelete.push_back(i);

                    std::sort(groupDelete.rbegin(), groupDelete.rend());

                    for (int i : groupDelete)
                        Geom.DeleteMesh(i);

                    OutHashMap[newName] = const_cast<xgeom_static::mesh_details*>(&group.m_MeshDetails);
                }

                for (const auto& um : Descriptor.m_UngroupMeshList)
                {
                    std::string fullName = um.m_NodePath + "/" + um.m_MeshName;
                    int idx = Geom.findMeshByPath(fullName);
                    if (idx != -1)
                    {
                        std::string newName = um.m_MeshDetails.m_Name;
                        Geom.m_Mesh[idx].m_Name = newName;
                        OutHashMap[newName] = const_cast<xgeom_static::mesh_details*>(&um.m_MeshDetails);
                    }
                }

                Geom.SortFacetsByMeshMaterialBone();
            }
        }

        //--------------------------------------------------------------------------------------

        void MergeMeshes()
        {
            //
            // Delete all the unwanted meshes
            //
            {
                std::vector<std::string> Path;
                std::function<void(const xgeom_static::details::node&, bool)> Recursive = [&](const xgeom_static::details::node& Node, bool bDelete )
                {
                    if (Path.empty()) Path.push_back(Node.m_Name);
                    else              Path.push_back( std::format("{}/{}", Path.back(), Node.m_Name ));

                    if ( bDelete == false )
                    {
                        if (m_Descriptor.isNodeInDeleteList(Path.back())) 
                            bDelete = true;
                    }

                    for (auto idx : Node.m_MeshList)
                    {
                        if (bDelete || m_Descriptor.isMeshInDeleteList(Path.back(), m_Details.m_MeshList[idx].m_Name))
                        {
                            std::string PathStr = std::format("{}/{}", Path.back(), m_Details.m_MeshList[idx].m_Name);
                            int         iRawMesh = m_RawGeom.findMeshByPath(PathStr);
                            if (iRawMesh != -1)
                            {
                                m_RawGeom.DeleteMesh(iRawMesh);
                            }
                        }
                    }

                    //
                    // Handle the children as well
                    //
                    for( auto& n : Node.m_Children)
                    {
                        Recursive(n, bDelete);
                    }

                    Path.pop_back();
                };

                Recursive(m_Details.m_RootNode, false);
            }

            //
            // Now we can actually merge the meshes
            //
            MergeMeshes(m_NameToDetail, m_RawGeom, m_Descriptor, m_Details);
        }

        //--------------------------------------------------------------------------------------

        //--------------------------------------------------------------------------------------
        // GI: the triangles with an edge longer than MaxEdge are split, a pass at a time until none is left. A long edge is split at its
        // midpoint by both of its triangles (the midpoint is shared by the edge's two vertices: no cracks, and an edge duplicated at a seam
        // gets the same midpoint position), a triangle with 1, 2 or 3 long edges becomes 2, 3 or 4, in its winding.
        // Returns the triangles added.
        std::size_t SplitTrianglesForGI(float MaxEdge, std::size_t MaxFacets)
        {
            auto&       V       = m_RawGeom.m_Vertex;
            const auto  nBefore = m_RawGeom.m_Facet.size();
            const float Max2    = MaxEdge * MaxEdge;
            const auto  Lerp    = [](const xraw3d::geom::vertex& A, const xraw3d::geom::vertex& B)
            {
                auto M = A;                                         // weights, frame, colors: the first's (a static mesh, no colors)
                M.m_Position = (A.m_Position + B.m_Position) * 0.5f;
                for (int i = 0; i < A.m_nUVs; ++i) M.m_UV[i] = (A.m_UV[i] + B.m_UV[i]) * 0.5f;
                for (int i = 0; i < A.m_nNormals; ++i)
                {
                    const auto N = A.m_BTN[i].m_Normal + B.m_BTN[i].m_Normal;
                    M.m_BTN[i].m_Normal = N.LengthSq() > 1e-12f ? N.NormalizeCopy() : A.m_BTN[i].m_Normal;
                }
                return M;                                           // tangents: recomputed (Mikk) after the split
            };

            for (int Pass = 0; Pass < 16; ++Pass)
            {
                std::unordered_map<std::uint64_t, int> Mid;           // edge (low, high vertex) -> its midpoint
                const auto MidOf = [&](int a, int b)
                {
                    if ((V[a].m_Position - V[b].m_Position).LengthSq() <= Max2) return -1;
                    const auto [It, bNew] = Mid.try_emplace((std::uint64_t(std::min(a, b)) << 32) | std::uint32_t(std::max(a, b)), int(V.size()));
                    if (bNew) V.push_back(Lerp(V[std::min(a, b)], V[std::max(a, b)]));
                    return It->second;
                };

                std::vector<xraw3d::geom::facet> Out;
                Out.reserve(m_RawGeom.m_Facet.size() * 2);
                const auto Emit = [&](const xraw3d::geom::facet& F, int a, int b, int c) { auto& T = Out.emplace_back(F); T.m_iVertex[0] = a; T.m_iVertex[1] = b; T.m_iVertex[2] = c; };
                const auto Dist2 = [&](int a, int b) { return (V[a].m_Position - V[b].m_Position).LengthSq(); };
                for (const auto& F : m_RawGeom.m_Facet)
                {
                    if (F.m_nVertices != 3) { Out.push_back(F); continue; }
                    const int v[3] = { F.m_iVertex[0], F.m_iVertex[1], F.m_iVertex[2] };
                    const int m[3] = { MidOf(v[0], v[1]), MidOf(v[1], v[2]), MidOf(v[2], v[0]) };     // edge k: v[k] -> v[k+1]
                    const int n    = (m[0] >= 0) + (m[1] >= 0) + (m[2] >= 0);
                    if (n == 0) { Out.push_back(F); continue; }
                    if (n == 3)
                    {
                        Emit(F, v[0], m[0], m[2]); Emit(F, m[0], v[1], m[1]); Emit(F, m[2], m[1], v[2]); Emit(F, m[0], m[1], m[2]);
                        continue;
                    }
                    for (int k = 0; k < 3; ++k)
                    {
                        const int a = v[k], b = v[(k + 1) % 3], c = v[(k + 2) % 3];
                        if (n == 1 && m[k] >= 0) { Emit(F, a, m[k], c); Emit(F, m[k], b, c); break; }
                        if (n == 2 && m[k] >= 0 && m[(k + 1) % 3] >= 0)
                        {
                            const int ab = m[k], bc = m[(k + 1) % 3];
                            Emit(F, ab, b, bc);
                            if (Dist2(ab, c) < Dist2(a, bc)) { Emit(F, a, ab, c); Emit(F, ab, bc, c); }      // the quad a ab bc c on its shorter diagonal
                            else                             { Emit(F, a, ab, bc); Emit(F, a, bc, c); }
                            break;
                        }
                    }
                }
                const bool bDone = Out.size() == m_RawGeom.m_Facet.size();
                if (Out.size() > MaxFacets)
                {
                    LogMessage(xresource_pipeline::msg_type::WARNING, std::format("GI: the triangle split stopped at {} triangles (the limit): a smaller mesh or a bigger SurfelSize reads the GI at more vertices", m_RawGeom.m_Facet.size()));
                    V.resize(V.size() - Mid.size());
                    break;
                }
                m_RawGeom.m_Facet = std::move(Out);
                if (bDone) break;
            }
            return m_RawGeom.m_Facet.size() - nBefore;
        }

        //--------------------------------------------------------------------------------------
        // GI: the surfels of the final geometry (its quantized vertices, in the GPU buffer's order: what the renderer reads), baked with the
        // renderer's own bake, saved in the geometry

        // The most surfels a mesh can have: they are indexed by 16 bits, 0xFFFF is "no surfel" (m_pGIVertSurfel)
        static constexpr std::size_t max_gi_surfels_v = 0xFFFE;
        static constexpr std::size_t section_owned_target_v = 48000;    // a GI section's owned surfels to aim at (its ghosts after: under the cap)

        // A suggested SurfelSize, rounded UP to 2 significant digits (0.0125 -> 0.013): with a fixed 2 decimals a small one read as the size that did not fit
        static float SuggestSurfelSize(float S) noexcept { const float P = std::pow(10.0f, std::floor(std::log10(S)) - 1); return std::ceil(S / P - 1e-3f) * P; }

        // GI: whether the bake fits (max_gi_surfels_v), checked on the mesh once scaled (and split), before anything else is made of it: the
        // bake's own count (surfel_mesh::CountSurfels: its surfels alone, stopped past the limit, seconds where the whole bake takes minutes).
        // Too many: a warning with the smallest SurfelSize that fits, and this compile has no GI (the rest of it as usual)
        void PrecheckGI()
        {
            if (m_Descriptor.m_GI.m_Mode == xgeom_static::gi_mode::OFF) return;
            if (const char* pU = std::getenv("XLION_GI_UNCAPPED"))     // measurement (GI chunks plan): the uncapped bake's memory, stage by stage
            {
                float R = 1; int Stop = 99;
                int Target = 48000;
                (void)std::sscanf(pU, "%f,%d,%d", &R, &Stop, &Target);
                using clock = std::chrono::steady_clock;
                const auto T0 = clock::now();
                FILE* pF = std::fopen("gi_autopsy.txt", "a");
                const auto Stage = [&](const char* pName, std::size_t Count)
                {
                    PROCESS_MEMORY_COUNTERS_EX M{}; M.cb = sizeof(M);
                    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&M), sizeof(M));
                    const auto Line = std::format("AUTOPSY {:7.1f} s  {:<28} count {:>12}  working set {:7.0f} MB  peak {:7.0f} MB  committed {:7.0f} MB\n",
                        std::chrono::duration<double>(clock::now() - T0).count(), pName, Count, M.WorkingSetSize / 1048576.0, M.PeakWorkingSetSize / 1048576.0, M.PrivateUsage / 1048576.0);
                    LogMessage(xresource_pipeline::msg_type::INFO, Line);
                    if (pF) { std::fputs(Line.c_str(), pF); std::fflush(pF); }
                };
                xlionrender::gi::surfel_mesh U;
                for (const auto& V : m_RawGeom.m_Vertex) { U.m_Pos.push_back(V.m_Position); U.m_Nrm.push_back(V.m_BTN[0].m_Normal); }
                for (const auto& F : m_RawGeom.m_Facet) U.m_Tris.push_back({ int(F.m_iVertex[0]), int(F.m_iVertex[1]), int(F.m_iVertex[2]) });
                U.m_bLeakProtection = m_Descriptor.m_GI.m_bLeakProtection;
                Stage("baseline (import + raw copy)", U.m_Tris.size());
                // the candidate rule, counted without allocating: per triangle max(1, area 4/R^2, longest edge / (R/2)); how many come from the floor of 1
                {
                    std::size_t nCand = 0, nFloor = 0, nTiny = 0; double Area = 0;
                    const float PerArea = 4.0f / (R * R);
                    for (const auto& T : U.m_Tris)
                    {
                        const auto& A = U.m_Pos[T[0]]; const auto& B = U.m_Pos[T[1]]; const auto& C = U.m_Pos[T[2]];
                        const float a = 0.5f * (B - A).Cross(C - A).Length(), Long = std::max({ (B - A).Length(), (C - B).Length(), (A - C).Length() });
                        const int n = std::min(1 << 16, std::max({ 1, int(std::ceil(a * PerArea)), int(std::ceil(Long / (0.5f * R))) }));
                        nCand += std::size_t(n); Area += a;
                        if (a * PerArea < 0.1f && Long < 0.1f * R) { ++nTiny; nFloor += 1; }
                    }
                    Stage("candidates by the rule", nCand);
                    Stage("  of which the floor of 1 on tiny triangles", nFloor);
                    Stage("  R-disks the area asks for", std::size_t(Area / (3.14159265 * R * R)));
                }
                if (Stop >= 1) { std::vector<xlionrender::gi::v3> CP, CN; U.Candidates(R, CP, CN, nullptr); Stage("candidates materialized", CP.size()); }
                if (Stop >= 2) { const std::size_t n = U.CountSurfels(R, ~std::size_t(0), false); Stage("Poisson count, no rays", n); }
                if (Stop >= 3) { U.BuildRays(); Stage("BVH built (Tri4 leaves)", U.m_Tri4.size()); }
                if (Stop >= 4)
                {
                    U.Bake(R, 3 * R, {}, ~std::size_t(0)); Stage("bake: fine surfels", U.m_Levels[0].size()); Stage("bake: parents", U.m_Levels[1].size());
                    const char* pName[] = { "candidates", "fine darts", "materials", "vertex surfels", "parents", "neighbours" };
                    for (int k = 0; k < 6; ++k) Stage(std::format("  bake ms: {}", pName[k]).c_str(), std::size_t(U.m_BakeMs[std::size_t(k)]));
                }
                if (Stop >= 5 && Stop != 8 && Stop != 9) { U.BuildReadGrid(); Stage("read grid: list words", U.m_ReadList.size()); Stage("read grid: table entries", U.m_ReadTable.size()); }
                if (Stop == 6 || Stop == 67)      // the bounded lists' light against the unbounded (job 1 of the GI handover)
                {
                    using namespace xlionrender::gi;
                    surfel_mesh V = U; V.m_ReadListCap = ~std::size_t(0); V.BuildReadGrid();
                    Stage("unbounded read grid: list words", V.m_ReadList.size());
                    lights Lt; Lt.m_SunDirection = v3{ 0.3f, -1, 0.4f }.NormalizeCopy(); Lt.m_SunColor = { 3, 3, 3 }; Lt.m_Sky = { 0.2f, 0.2f, 0.25f };
                    world Wa, Wb, Wc;
                    const int Ia = Wa.Add(V, xmath::fmat4::fromIdentity(), { 0.6f, 0.6f, 0.6f }), Ib = Wb.Add(V, xmath::fmat4::fromIdentity(), { 0.6f, 0.6f, 0.6f });
                    const int Ic = Wc.Add(U, xmath::fmat4::fromIdentity(), { 0.6f, 0.6f, 0.6f });
                    const auto Run = [&](world& W, unsigned Seed)
                    {
                        W.m_Setup.m_RaysPerSurfel = 32; W.m_Setup.m_MinBlend = 1;
                        for (int u = 0; u < 8; ++u) W.Update(Lt, { 0, 2, 0 }, 1 << 30, Seed + unsigned(u));
                        W.m_Setup.m_MinBlend = 0;
                        for (int u = 0; u < 60; ++u) W.Update(Lt, { 0, 2, 0 }, 1 << 30, Seed + 100 + unsigned(u));
                    };
                    Run(Wa, 1000); Stage("world a converged", 0); Run(Wb, 5000); Run(Wc, 1000); Stage("worlds b, c converged", 0);
                    const auto Lum = [](const v3& e) { return 0.2126 * e.m_X + 0.7152 * e.m_Y + 0.0722 * e.m_Z; };
                    std::vector<double> Dab, Dac; double Top = 1e-9;
                    for (std::size_t t = 0; t < U.m_Tris.size(); t += 7)
                    {
                        const auto& T = U.m_Tris[t];
                        const v3 Pp = (U.m_Pos[T[0]] + U.m_Pos[T[1]] + U.m_Pos[T[2]]) * (1.0f / 3.0f);
                        const v3 F = (U.m_Pos[T[1]] - U.m_Pos[T[0]]).Cross(U.m_Pos[T[2]] - U.m_Pos[T[0]]);
                        if (F.Length() < 1e-12f) continue;
                        const v3 Nn = F.NormalizeCopy();
                        const auto qa = Wa.ReadPoint(Ia, Pp, Nn), qb = Wb.ReadPoint(Ib, Pp, Nn), qc = Wc.ReadPoint(Ic, Pp, Nn);
                        if (!qa.m_bSupported || !qb.m_bSupported || !qc.m_bSupported) continue;
                        Top = std::max(Top, Lum(qa.m_E));
                        Dab.push_back(std::abs(Lum(qa.m_E) - Lum(qb.m_E))); Dac.push_back(std::abs(Lum(qa.m_E) - Lum(qc.m_E)));
                    }
                    const auto Pct = [&](std::vector<double> X, double p) { std::sort(X.begin(), X.end()); return X.empty() ? 0.0 : 100 * X[std::size_t(p * double(X.size() - 1))] / Top; };
                    const auto Line = std::format("LISTCAP at {} points (of the brightest): bounded off unbounded {:.2f}% median, {:.2f}% 95th, {:.2f}% 99th; two unbounded worlds {:.2f}%, {:.2f}%, {:.2f}%\n",
                        Dac.size(), Pct(Dac, 0.5), Pct(Dac, 0.95), Pct(Dac, 0.99), Pct(Dab, 0.5), Pct(Dab, 0.95), Pct(Dab, 0.99));
                    LogMessage(xresource_pipeline::msg_type::INFO, Line);
                    if (pF) { std::fputs(Line.c_str(), pF); std::fflush(pF); }
                }
                if (Stop == 7)      // the memory ledger by category (job 3 of the GI handover)
                {
                    using namespace xlionrender::gi;
                    const auto B = [](const auto& V) { return double(V.capacity() * sizeof(V[0])); };
                    const double nF = double(std::max<std::size_t>(U.m_Levels[0].size(), 1));
                    double Nb = 0; for (const auto& N : U.m_Neighbors) Nb += B(N.m_First) + B(N.m_Index) + B(N.m_Weight);
                    const double Tri  = B(U.m_Pos) + B(U.m_Nrm) + B(U.m_Tris) + B(U.m_UV) + B(U.m_TriMaterial);
                    const double Bvh  = B(U.m_Bvh.m_Nodes) + B(U.m_Bvh.m_Leaves) + B(U.m_Tri4);
                    const double Surf = B(U.m_Levels[0]) + B(U.m_Levels[1]) + B(U.m_ParentOf) + B(U.m_SurfelMaterial);
                    const double Vert = B(U.m_VertSurfels), Orig = B(U.m_OriginFirst) + B(U.m_OriginVert);
                    const double Read = B(U.m_ReadTable) + B(U.m_ReadList);
                    std::string Line = std::format("LEDGER {} fine surfels, {} parents, {} triangles. Shared per mesh, MB (B a fine surfel):\n", U.m_Levels[0].size(), U.m_Levels[1].size(), U.m_Tris.size());
                    for (const auto& [pN, v] : std::initializer_list<std::pair<const char*, double>>{ { "triangles (CPU twin)", Tri }, { "BVH + Tri4", Bvh }, { "surfels + parents", Surf },
                        { "vertex surfels", Vert }, { "neighbour lists", Nb }, { "ray origins", Orig }, { "read grid", Read } })
                        Line += std::format("LEDGER   {:<22} {:8.2f} MB  ({:6.1f} B)\n", pN, v / 1048576.0, v / nF);
                    const double GISum = Surf + Vert + Nb + Orig + Read;
                    Line += std::format("LEDGER   GI shared (without triangles, BVH) {:8.2f} MB  ({:6.1f} B)\n", GISum / 1048576.0, GISum / nF);
                    for (const bool bOwned : { false, true })       // per instance, the CPU world's light state (its stats, level 0)
                    {
                        world W; W.m_Setup.m_bGPUOwned = W.m_Setup.m_bGPUTrace = W.m_Setup.m_bGPUShown = bOwned;     // (as the renderer: all three)
                        (void)W.Add(U, xmath::fmat4::fromIdentity(), { 0.6f, 0.6f, 0.6f });
                        lights Lt;
                        const auto St = W.Update(Lt, { 0, 2, 0 }, 0, 1);
                        Line += std::format("LEDGER   per instance, the CPU's light state, {}: {:8.2f} MB ({:6.1f} B), the filter {:6.1f} B, links {:6.1f} B\n", bOwned ? "GPU-owned " : "CPU-solved",
                            double(St.m_LightBytes) / 1048576.0, double(St.m_LightBytes) / nF, double(St.m_FilterBytes) / nF, double(St.m_LinkBytes) / nF);
                    }
                    LogMessage(xresource_pipeline::msg_type::INFO, Line);
                    if (pF) { std::fputs(Line.c_str(), pF); std::fflush(pF); }
                }
                if (Stop == 8)      // surfel LOD: coarser levels from the parents, by the bake's rule (GI working set: far field option A)
                {
                    using namespace xlionrender::gi;
                    std::vector<surfel> Lv = U.m_Levels[1];
                    float Rl = 3 * R;
                    for (int l = 0; l < 2; ++l)
                    {
                        const float Rn = 3 * Rl;
                        std::vector<int> Order(Lv.size());
                        for (std::size_t i = 0; i < Order.size(); ++i) Order[i] = int(i);
                        rng Shuffle(0x5EEDu);
                        for (std::size_t i = Order.size(); i > 1; --i) std::swap(Order[i - 1], Order[std::size_t(Shuffle.Next() * float(i)) % i]);
                        surfel_grid G{ Rn, {} };
                        std::vector<surfel> Out;
                        for (const int i : Order)
                        {
                            const v3 Pi = Lv[std::size_t(i)].m_Center, Ni = Lv[std::size_t(i)].m_Normal;
                            bool bTaken = false;
                            G.Near(Pi, 1, [&](int k) { const auto& S = Out[std::size_t(k)]; bTaken = bTaken || ((Pi - S.m_Center).Length() < Rn && S.m_Normal.Dot(Ni) > 0.8f
                                                                     && std::abs((S.m_Center - Pi).Dot(Ni)) < 0.1f * Rn && U.Sees(Pi, Ni, S)); });
                            if (bTaken) continue;
                            G.Add(Pi, int(Out.size()));
                            Out.push_back({ Pi, Ni, Rn });
                        }
                        Stage(std::format("LOD level {} ({:.0f} m): surfels, x{:.2f} fewer", l + 2, Rn, double(Lv.size()) / double(std::max<std::size_t>(Out.size(), 1))).c_str(), Out.size());
                        Lv = std::move(Out); Rl = Rn;
                    }
                }
                if (Stop == 9)      // sections: the bake partitioned into GI chunks (gi_chunks_plan.md)
                {
                    using namespace xlionrender::gi;
                    std::vector<int> TriCluster(U.m_Tris.size());
                    std::unordered_map<std::uint64_t, int> CellId;
                    for (std::size_t t = 0; t < U.m_Tris.size(); ++t)
                    {
                        const auto& T = U.m_Tris[t];
                        const v3 C = (U.m_Pos[T[0]] + U.m_Pos[T[1]] + U.m_Pos[T[2]]) * (1.0f / 3.0f);
                        const std::uint64_t K = surfel_grid::Key(int(std::floor(C.m_X / 4.0f)), int(std::floor(C.m_Y / 4.0f)), int(std::floor(C.m_Z / 4.0f)));
                        TriCluster[t] = CellId.try_emplace(K, int(CellId.size())).first->second;
                    }
                    Stage("chunks: proxy clusters (4 m cells)", CellId.size());
                    chunked_mesh Ch; chunk_report Rep;
                    const bool bFits = PartitionChunks(U, TriCluster, int(CellId.size()), std::size_t(Target), Ch, &Rep);
                    std::size_t Words = 0; for (const auto& C : Ch.m_Chunks) Words += C.m_ReadList.size();
                    const auto Line = std::format("CHUNKS target {}: {} chunks ({} split again), owned {}..{}, ghosts {} ({:.0f}% of the owned; at most {} a chunk), local at most {} (fits {}), "
                                                  "read lists {} words in all; partition {:.0f} ms, chunks built {:.0f} ms\n",
                        Target, Rep.m_nChunks, Rep.m_nSplitsAgain, Rep.m_OwnedMin, Rep.m_OwnedMax, Rep.m_GhostsTotal, 100.0 * double(Rep.m_GhostsTotal) / double(std::max<std::size_t>(U.m_Levels[0].size(), 1)),
                        Rep.m_GhostsMax, Rep.m_LocalMax, bFits ? "yes" : "NO", Words, Rep.m_PartitionMs, Rep.m_BuildMs);
                    LogMessage(xresource_pipeline::msg_type::INFO, Line);
                    if (pF) { std::fputs(Line.c_str(), pF); std::fflush(pF); }
                    Stage("chunks: built", Ch.m_Chunks.size());
                    {
                        world Ww, Wc;
                        (void)Ww.Add(U, xmath::fmat4::fromIdentity(), { 0.5f, 0.5f, 0.5f });
                        for (const auto& C : Ch.m_Chunks) (void)Wc.Add(C, xmath::fmat4::fromIdentity(), { 0.5f, 0.5f, 0.5f });
                        lights Lt;
                        (void)Ww.Update(Lt, { 0, 2, 0 }, 0, 1); (void)Wc.Update(Lt, { 0, 2, 0 }, 0, 1);      // (their TLAS)
                        const std::size_t nRays = 1 << 21;
                        std::vector<std::pair<v3, v3>> Rays(nRays);
                        rng Rr(77);
                        const auto& L0 = U.m_Levels[0];
                        for (auto& [O, D] : Rays) { const auto& S = L0[std::min<std::size_t>(std::size_t(Rr.Next() * float(L0.size())), L0.size() - 1)]; O = S.m_Center + S.m_Normal * 1e-3f; D = world::CosineDir(S.m_Normal, Rr); }
                        std::vector<float> Tw(nRays), Tc(nRays);
                        const auto Time = [&](const world& W, std::vector<float>& T)
                        {
                            const auto T0 = std::chrono::steady_clock::now();
                            ParallelFor(int(nRays / 1024), [&](int b) { for (std::size_t k = std::size_t(b) * 1024; k < std::size_t(b + 1) * 1024; ++k) { const auto H = W.Cast(Rays[k].first, Rays[k].second); T[k] = H.m_Inst < 0 ? -1.0f : H.m_T; } });
                            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
                        };
                        const double Mw = Time(Ww, Tw), Mc = Time(Wc, Tc);
                        std::size_t nDiff = 0; for (std::size_t k = 0; k < nRays; ++k) nDiff += std::abs(Tw[k] - Tc[k]) > 1e-3f;
                        const auto L3 = std::format("CHUNKS overlap: {} rays, whole mesh {:.0f} ms ({:.0f} rays/ms), {} sections {:.0f} ms ({:.0f} rays/ms, x{:.2f}); hits that differ {}\n",
                            nRays, Mw, double(nRays) / Mw, Ch.m_Chunks.size(), Mc, double(nRays) / Mc, Mc / Mw, nDiff);
                        LogMessage(xresource_pipeline::msg_type::INFO, L3);
                        if (pF) { std::fputs(L3.c_str(), pF); std::fflush(pF); }
                    }
                    {
                        std::size_t nGhost = 0, nUsed = 0, nUsed2 = 0;
                        for (std::size_t c = 0; c < Ch.m_Chunks.size(); ++c)
                        {
                            const auto& C = Ch.m_Chunks[c];
                            const std::uint32_t nOwned = Ch.m_nOwned[c], nAll = std::uint32_t(C.m_Levels[0].size());
                            std::vector<char> Used(nAll, 0);
                            for (const auto& E : C.m_ReadTable)
                            {
                                if (E[1] == 0xFFFFFFFFu) continue;
                                for (std::uint32_t p = 0; p < E[3]; ++p)
                                {
                                    const std::uint32_t First = C.m_ReadList[E[2] + 4 * p + 2], n = C.m_ReadList[E[2] + 4 * p + 3] & 0xFFFFu;
                                    for (std::uint32_t k = 0; k < n; ++k) Used[C.m_ReadList[First + k] & 0xFFFFu] = 1;
                                }
                            }
                            for (const auto& V : C.m_VertSurfels) for (const int sv : V) if (sv >= 0) Used[std::size_t(sv)] = 1;
                            const auto& Nb = C.m_Neighbors[0];
                            if (Nb.m_First.size() == nAll + 1) for (std::uint32_t o = 0; o < nOwned; ++o) for (std::uint32_t k = Nb.m_First[o]; k < Nb.m_First[o + 1]; ++k) Used[Nb.m_Index[k]] = 1;
                            std::vector<char> Used2 = Used;      // one more filter pass: the neighbours of the used ghosts
                            if (Nb.m_First.size() == nAll + 1) for (std::uint32_t g = nOwned; g < nAll; ++g) if (Used[g]) for (std::uint32_t k = Nb.m_First[g]; k < Nb.m_First[g + 1]; ++k) Used2[Nb.m_Index[k]] = 1;
                            for (std::uint32_t g = nOwned; g < nAll; ++g) { ++nGhost; nUsed += Used[g]; nUsed2 += Used2[g]; }
                        }
                        const auto L2 = std::format("CHUNKS ghosts named by the reads, vertex surfels and owned neighbours: {} of {} ({:.0f}% of the owned); with one more filter pass {} ({:.0f}%)\n",
                            nUsed, nGhost, 100.0 * double(nUsed) / double(std::max<std::size_t>(U.m_Levels[0].size(), 1)), nUsed2, 100.0 * double(nUsed2) / double(std::max<std::size_t>(U.m_Levels[0].size(), 1)));
                        LogMessage(xresource_pipeline::msg_type::INFO, L2);
                        if (pF) { std::fputs(L2.c_str(), pF); std::fflush(pF); }
                    }
                }
                if (Stop == 10)     // the compression census
                {
                    using namespace xlionrender::gi;
                    std::string Out;
                    const auto Add = [&](std::string L) { Out += "CENSUS " + L + "\n"; };
                    const auto Arr = [&](const char* pN, const auto& V)
                    {
                        Add(std::format("{:<26} count {:>10}  capacity {:>10}  element {:>3} B  bytes {:8.2f} MB  (unused capacity {:5.1f}%)", pN, V.size(), V.capacity(), sizeof(V[0]),
                            double(V.capacity() * sizeof(V[0])) / 1048576.0, V.capacity() ? 100.0 * double(V.capacity() - V.size()) / double(V.capacity()) : 0.0));
                    };
                    Arr("fine surfels", U.m_Levels[0]); Arr("parents", U.m_Levels[1]); Arr("parent of", U.m_ParentOf); Arr("surfel materials", U.m_SurfelMaterial);
                    Arr("vertex surfels", U.m_VertSurfels); Arr("origin first", U.m_OriginFirst); Arr("origin vertices", U.m_OriginVert);
                    for (int Lv = 0; Lv < 2; ++Lv) { Arr(Lv ? "neighbours L1 first" : "neighbours L0 first", U.m_Neighbors[Lv].m_First); Arr(Lv ? "neighbours L1 index" : "neighbours L0 index", U.m_Neighbors[Lv].m_Index); Arr(Lv ? "neighbours L1 weight" : "neighbours L0 weight", U.m_Neighbors[Lv].m_Weight); }
                    Arr("read table", U.m_ReadTable); Arr("read list words", U.m_ReadList);
                    const double nF = double(U.m_Levels[0].size()), nV = double(U.m_Pos.size());
                    Add(std::format("{} fine surfels, {} parents, {} vertices ({:.2f} a fine surfel), {} triangles", U.m_Levels[0].size(), U.m_Levels[1].size(), U.m_Pos.size(), nV / nF, U.m_Tris.size()));
                    // origins
                    {
                        std::array<std::size_t, 9> H{};
                        for (std::size_t k = 0; k + 1 < U.m_OriginFirst.size(); ++k) ++H[std::min<std::size_t>(U.m_OriginFirst[k + 1] - U.m_OriginFirst[k], 8)];
                        std::string L = "origins per surfel (incl. its center):";
                        for (int k = 0; k < 9; ++k) L += std::format(" {}:{:.1f}%", k, 100.0 * double(H[std::size_t(k)]) / nF);
                        Add(L);
                    }
                    // vertex bindings
                    {
                        std::unordered_set<std::uint64_t> Tup;
                        std::size_t nNone = 0;
                        const auto Pack = [](const std::array<int, 4>& T) { std::uint64_t h = 1469598103934665603ull; for (const int x : T) { h ^= std::uint64_t(std::uint32_t(x)); h *= 1099511628211ull; } return h; };
                        std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>> PerCell;
                        std::unordered_map<std::uint64_t, std::pair<int, int>> Span;
                        for (std::size_t v = 0; v < U.m_VertSurfels.size(); ++v)
                        {
                            const auto& T = U.m_VertSurfels[v];
                            if (T[0] < 0) { ++nNone; continue; }
                            Tup.insert(Pack(T));
                            const auto& P = U.m_Pos[v];
                            const std::uint64_t K = surfel_grid::Key(int(std::floor(P.m_X / 4.0f)), int(std::floor(P.m_Y / 4.0f)), int(std::floor(P.m_Z / 4.0f)));
                            PerCell[K].insert(Pack(T));
                            auto [It, bNew] = Span.try_emplace(K, std::pair{ T[0], T[0] });
                            for (const int x : T) if (x >= 0) { It->second.first = std::min(It->second.first, x); It->second.second = std::max(It->second.second, x); }
                        }
                        std::size_t SumCell = 0, MaxCell = 0, Wide = 0;
                        for (const auto& [K, S] : PerCell) { SumCell += S.size(); MaxCell = std::max(MaxCell, S.size()); }
                        for (const auto& [K, Sp] : Span) Wide += (Sp.second - Sp.first) > 255;
                        Add(std::format("vertex bindings: {} vertices, {} with none ({:.1f}%), {} unique ordered tuples ({:.1f}% of the vertices); per 4 m cell: {} cells, tuples {:.1f} a cell on average, {} at most; cells whose index span exceeds 255: {:.1f}%",
                            U.m_VertSurfels.size(), nNone, 100.0 * double(nNone) / nV, Tup.size(), 100.0 * double(Tup.size()) / nV, PerCell.size(), double(SumCell) / double(std::max<std::size_t>(PerCell.size(), 1)), MaxCell, 100.0 * double(Wide) / double(std::max<std::size_t>(Span.size(), 1))));
                    }
                    // read grid
                    {
                        std::size_t nCells = 0, nParts = 0, nEnt = 0, nFull = 0;
                        std::unordered_map<std::uint16_t, std::size_t> Pat;
                        std::unordered_map<std::uint64_t, std::size_t> Lists;
                        for (const auto& E : U.m_ReadTable)
                        {
                            if (E[1] == 0xFFFFFFFFu) continue;
                            ++nCells;
                            for (std::uint32_t p = 0; p < E[3]; ++p)
                            {
                                ++nParts;
                                const std::uint32_t F = U.m_ReadList[E[2] + 4 * p + 2], n = U.m_ReadList[E[2] + 4 * p + 3] & 0xFFFFu;
                                std::uint64_t h = 1469598103934665603ull ^ n;
                                for (std::uint32_t k = 0; k < n; ++k)
                                {
                                    const std::uint32_t w = U.m_ReadList[F + k];
                                    ++nEnt; ++Pat[std::uint16_t(w >> 16)]; nFull += (w >> 16) == 0xFFFFu;
                                    h ^= w; h *= 1099511628211ull;
                                }
                                ++Lists[h];
                            }
                        }
                        std::vector<std::size_t> Counts; for (const auto& [k, n] : Pat) Counts.push_back(n);
                        std::sort(Counts.rbegin(), Counts.rend());
                        std::size_t Top16 = 0, Top256 = 0; for (std::size_t k = 0; k < Counts.size(); ++k) { if (k < 16) Top16 += Counts[k]; if (k < 256) Top256 += Counts[k]; }
                        std::size_t nDupLists = 0, nDupWords = 0;
                        for (const auto& [h, n] : Lists) if (n > 1) { nDupLists += n - 1; }
                        (void)nDupWords;
                        const double Dir = double(U.m_ReadTable.size()) * 16.0 + double(nParts) * 16.0, Pay = double(nEnt) * 4.0;
                        Add(std::format("read grid: {} cells (table {} slots: {:.0f}% used), {} parts ({:.1f} a cell), {} entries ({:.1f} a part); directory (table + part headers) {:.2f} MB, payload {:.2f} MB",
                            nCells, U.m_ReadTable.size(), 100.0 * double(nCells) / double(std::max<std::size_t>(U.m_ReadTable.size(), 1)), nParts, double(nParts) / double(std::max<std::size_t>(nCells, 1)),
                            nEnt, double(nEnt) / double(std::max<std::size_t>(nParts, 1)), Dir / 1048576.0, Pay / 1048576.0));
                        Add(std::format("corner weights: 0xFFFF (all seen) {:.1f}% of the entries; {} distinct patterns; the top 16 cover {:.1f}%, the top 256 {:.1f}%; identical lists (exact entries): {} of {} parts are repeats",
                            100.0 * double(nFull) / double(std::max<std::size_t>(nEnt, 1)), Counts.size(), 100.0 * double(Top16) / double(std::max<std::size_t>(nEnt, 1)), 100.0 * double(Top256) / double(std::max<std::size_t>(nEnt, 1)), nDupLists, nParts));
                    }
                    // neighbours
                    for (int Lv = 0; Lv < 2; ++Lv)
                    {
                        const auto& Nb = U.m_Neighbors[Lv];
                        std::size_t Max = 0; std::array<std::size_t, 5> H{};
                        for (std::size_t k = 0; k + 1 < Nb.m_First.size(); ++k) { const std::size_t d = Nb.m_First[k + 1] - Nb.m_First[k]; Max = std::max(Max, d); ++H[std::min<std::size_t>(d / 8, 4)]; }
                        const double n = double(std::max<std::size_t>(Nb.m_First.size() - 1, 1));
                        Add(std::format("neighbours L{}: degree mean {:.1f}, max {}; 0-7 {:.0f}%, 8-15 {:.0f}%, 16-23 {:.0f}%, 24-31 {:.0f}%, 32+ {:.0f}%", Lv, double(Nb.m_Index.size()) / n, Max,
                            100.0 * double(H[0]) / n, 100.0 * double(H[1]) / n, 100.0 * double(H[2]) / n, 100.0 * double(H[3]) / n, 100.0 * double(H[4]) / n));
                    }
                    LogMessage(xresource_pipeline::msg_type::INFO, Out);
                    if (pF) { std::fputs(Out.c_str(), pF); std::fflush(pF); }
                }
                if (pF) std::fclose(pF);
            }
            xlionrender::gi::surfel_mesh M;
            BBox3 Box;
            M.m_Pos.reserve(m_RawGeom.m_Vertex.size());
            M.m_Nrm.reserve(m_RawGeom.m_Vertex.size());
            for (const auto& V : m_RawGeom.m_Vertex) { M.m_Pos.push_back(V.m_Position); M.m_Nrm.push_back(V.m_BTN[0].m_Normal); Box.Update(V.m_Position); }
            M.m_Tris.reserve(m_RawGeom.m_Facet.size());
            for (const auto& F : m_RawGeom.m_Facet) M.m_Tris.push_back({ int(F.m_iVertex[0]), int(F.m_iVertex[1]), int(F.m_iVertex[2]) });

            const float       R = std::max(m_Descriptor.m_GI.m_SurfelSize, 0.01f);
            // Without the rays first (cheap, and a lower bound: over the limit there is over it in the bake), then the bake's own count
            std::size_t n = M.CountSurfels(R, max_gi_surfels_v, false);
            if (n <= max_gi_surfels_v) n = M.CountSurfels(R, max_gi_surfels_v);
            if (n <= max_gi_surfels_v)
            {
                LogMessage(xresource_pipeline::msg_type::INFO, std::format("GI: {} surfels at SurfelSize {} (a mesh can have {})", n, R, max_gi_surfels_v));
                if (const char* pF = std::getenv("XLION_GI_FORCE_SECTIONS"))      // (tests, measurements: sections of about that many owned surfels)
                {
                    m_bGISections = true;
                    BuildGIRegions(M, R, std::size_t(std::max(1, std::atoi(pF))));
                }
                return;
            }

            // Over one mesh's cap: baked whole and cut into sections (BakeGI, gi_chunks_plan.md), each under it. A section is whole clusters,
            // and the clusters (ConvertToGeom) are cut at the sections' regions: boxes of the mesh, a k-d split of its triangles by the bake's
            // candidates (about six to a surfel on Police and the Bistro), so a section is a compact piece of the mesh
            BuildGIRegions(M, R, section_owned_target_v);
            const auto Size = Box.m_MaxPos - Box.m_MinPos;
            LogMessage(xresource_pipeline::msg_type::INFO, std::format("GI: at SurfelSize {} this mesh needs more than {} surfels (one GI mesh's most): it is baked whole and cut into sections. The mesh is {:.1f} x {:.1f} x {:.1f} units, {} triangles"
                      , R, max_gi_surfels_v, Size.m_X, Size.m_Y, Size.m_Z, M.m_Tris.size()));
            m_bGISections = true;
        }

        // The GI sections' regions (PrecheckGI): a k-d tree over the triangles' centres, each leaf about section_owned_target_v surfels' worth of
        // the bake's candidates (its rule: by area, by the longest edge, at least one)
        struct gi_region_node { int m_Axis = -1; float m_Split = 0; int m_Left = -1, m_Right = -1, m_Leaf = -1; };
        void BuildGIRegions(const xlionrender::gi::surfel_mesh& M, float R, std::size_t Owned)
        {
            m_GIRegions.clear(); m_nGIRegions = 0;
            const float PerArea = 4.0f / (R * R);
            std::vector<std::uint32_t> Tris(M.m_Tris.size());
            std::vector<float> W(M.m_Tris.size());
            std::vector<xlionrender::gi::v3> C(M.m_Tris.size());
            for (std::size_t t = 0; t < M.m_Tris.size(); ++t)
            {
                const auto& T = M.m_Tris[t];
                const auto& A = M.m_Pos[T[0]]; const auto& B = M.m_Pos[T[1]]; const auto& Cc = M.m_Pos[T[2]];
                const float a = 0.5f * (B - A).Cross(Cc - A).Length(), Long = std::max({ (B - A).Length(), (Cc - B).Length(), (A - Cc).Length() });
                W[t] = float(std::max({ 1, int(std::ceil(a * PerArea)), int(std::ceil(Long / (0.5f * R))) }));
                C[t] = (A + B + Cc) * (1.0f / 3.0f);
                Tris[t] = std::uint32_t(t);
            }
            const double Target = double(Owned) * 5.0;
            const auto Split = [&](auto&& Self, std::size_t b, std::size_t e) -> int
            {
                const int Node = int(m_GIRegions.size());
                m_GIRegions.push_back({});
                double Sum = 0; for (std::size_t k = b; k < e; ++k) Sum += W[Tris[k]];
                if (Sum <= Target || e - b < 2) { m_GIRegions[std::size_t(Node)].m_Leaf = m_nGIRegions++; return Node; }
                xlionrender::gi::v3 Lo{ 1e30f, 1e30f, 1e30f }, Hi{ -1e30f, -1e30f, -1e30f };
                for (std::size_t k = b; k < e; ++k) { Lo = xlionrender::gi::v3::Min(Lo, C[Tris[k]]); Hi = xlionrender::gi::v3::Max(Hi, C[Tris[k]]); }
                const auto E = Hi - Lo;
                const int Ax = E.m_X >= E.m_Y && E.m_X >= E.m_Z ? 0 : E.m_Y >= E.m_Z ? 1 : 2;
                const auto At = [&](std::uint32_t t) { return Ax == 0 ? C[t].m_X : Ax == 1 ? C[t].m_Y : C[t].m_Z; };
                std::sort(Tris.begin() + std::ptrdiff_t(b), Tris.begin() + std::ptrdiff_t(e), [&](std::uint32_t x, std::uint32_t y) { return At(x) != At(y) ? At(x) < At(y) : x < y; });
                double Acc = 0; std::size_t m = b;
                while (m + 1 < e && Acc + W[Tris[m]] <= Sum / 2) Acc += W[Tris[m++]];
                m = std::max(m, b + 1);
                const float Plane = 0.5f * (At(Tris[m - 1]) + At(Tris[m]));
                const int L = Self(Self, b, m), Rr = Self(Self, m, e);
                auto& N = m_GIRegions[std::size_t(Node)];
                N.m_Axis = Ax; N.m_Split = Plane; N.m_Left = L; N.m_Right = Rr;
                return Node;
            };
            Split(Split, 0, Tris.size());
        }
        int GIRegionOf(const xmath::fvec3& P) const noexcept
        {
            int n = 0;
            while (n >= 0 && n < int(m_GIRegions.size()) && m_GIRegions[std::size_t(n)].m_Leaf < 0)
            {
                const auto& N = m_GIRegions[std::size_t(n)];
                n = (N.m_Axis == 0 ? P.m_X : N.m_Axis == 1 ? P.m_Y : P.m_Z) < N.m_Split ? N.m_Left : N.m_Right;
            }
            return n >= 0 && n < int(m_GIRegions.size()) ? m_GIRegions[std::size_t(n)].m_Leaf : 0;
        }

        // Over one GI mesh's cap: the canonical bake over the whole geometry, then cut into sections of whole clusters (PartitionChunks: each
        // owns some surfels, copies the ones near its border it reads as ghosts, and has its own read grid, neighbours and origins built with
        // the whole geometry's visibility), saved in xgeom_static version 7's section arrays
        void BakeGISections(xlionrender::gi::surfel_mesh& M, float R)
        {
            namespace gi = xlionrender::gi;
            const auto T0 = std::chrono::steady_clock::now();
            const auto Progress = [&](const std::string& What) { LogMessage(xresource_pipeline::msg_type::INFO, std::format("GI sections, {:.1f} s: {}", std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count(), What)); };
            Progress(std::format("baking {} triangles, {} vertices, {} clusters", M.m_Tris.size(), M.m_Pos.size(), m_FinalGeom.m_nClusters));
            {   // the section borders' planes (the regions' k-d splits, mesh space): where to look for a seam
                std::string Line = "region splits (depth: axis = value):";
                const auto Walk = [&](auto& Self, int n, int Depth) -> void
                {
                    if (n < 0 || n >= int(m_GIRegions.size()) || m_GIRegions[std::size_t(n)].m_Leaf >= 0 || Depth > 3) return;
                    const auto& N = m_GIRegions[std::size_t(n)];
                    Line += std::format(" {}:{}={:.2f}", Depth, "XYZ"[N.m_Axis], N.m_Split);
                    Self(Self, N.m_Left, Depth + 1); Self(Self, N.m_Right, Depth + 1);
                };
                Walk(Walk, 0, 0);
                Progress(Line);
            }
            // the progress bar: the bake the first 40%, the sections the next 55%, their packing the rest
            M.m_OnBakeProgress = [this](float f) { displayProgressBar("Baking the GI surfels", 0.40f * f); };
            M.Bake(R, 3 * R);
            M.m_OnBakeProgress = nullptr;
            Progress(std::format("baked: {} surfels, {} parents; bake ms: candidates {:.0f}, darts {:.0f}, vertex surfels {:.0f}, parents {:.0f}, neighbours {:.0f}", M.m_Levels[0].size(), M.m_Levels[1].size(),
                M.m_BakeMs[0], M.m_BakeMs[1], M.m_BakeMs[3], M.m_BakeMs[4], M.m_BakeMs[5]));
            const auto TriCluster = gi::TriClustersOf(m_FinalGeom);
            const std::size_t Target = section_owned_target_v;      // (a region over the cap after its ghosts is split again by it)
            gi::chunked_mesh Ch; gi::chunk_report Rep;
            Rep.m_OnProgress = [this](float f) { displayProgressBar("Baking the GI surfels", 0.40f + 0.55f * f); };
            m_ClusterRegion.resize(m_FinalGeom.m_nClusters, 0);
            const bool bFits = gi::PartitionChunks(M, TriCluster, int(m_FinalGeom.m_nClusters), Target, Ch, &Rep, m_ClusterRegion);
            if (!bFits || Ch.m_Chunks.empty() || Ch.m_Chunks.size() > 0xFFFF)
            {
                LogMessage(xresource_pipeline::msg_type::WARNING, std::format("GI turned OFF for this mesh: {} surfels at SurfelSize {} could not be cut into sections under {} each (a single cluster holds too many: raise SurfelSize there)", M.m_Levels[0].size(), R, gi::chunk_local_cap_v));
                m_FinalGeom.m_GIMode = xgeom_static::gi_mode::OFF;
                return;
            }
            const auto F = gi::FlattenSections(Ch, m_FinalGeom.m_nVertices);
            {   // what the sections' packed structures hold (the load-time data), MB
                double Table = 0, Heads = 0, Lists = 0, Nb = 0, Orig = 0;
                for (const auto& C : Ch.m_Chunks)
                {
                    std::size_t nParts = 0;
                    for (const auto& E : C.m_ReadTable) if (E[1] != 0xFFFFFFFFu) nParts += E[3];
                    Table += double(C.m_ReadTable.size()) * 16; Heads += double(nParts) * 16; Lists += double(C.m_ReadList.size() - 4 * nParts) * 4;
                    for (const auto& N : C.m_Neighbors) Nb += double(N.m_First.size() + 2 * N.m_Index.size()) * 4;
                    Orig += double(C.m_OriginFirst.size() + C.m_OriginVert.size()) * 4;
                }
                const double M = 1.0 / 1048576.0;
                Progress(std::format("packed structures {:.1f} MB: read tables {:.1f}, part headers {:.1f}, list entries {:.1f}, neighbours {:.1f}, origins {:.1f}",
                    double(F.m_Derived.size()) * 4 * M, Table * M, Heads * M, Lists * M, Nb * M, Orig * M));
                // the lists' census (what a smaller list form could drop): parts, entries, all-seen corner weights, lists equal to another's
                std::size_t nParts = 0, nEnt = 0, nAllSeen = 0, nUniqueEnt = 0, nUniqueSetEnt = 0, nUnique = 0, nUniqueSet = 0;
                for (const auto& C : Ch.m_Chunks)
                {
                    std::unordered_set<std::uint64_t> Lists, Sets;
                    for (const auto& E : C.m_ReadTable)
                    {
                        if (E[1] == 0xFFFFFFFFu) continue;
                        for (std::uint32_t p = 0; p < E[3]; ++p)
                        {
                            const std::uint32_t First = C.m_ReadList[E[2] + 4 * p + 2], n = C.m_ReadList[E[2] + 4 * p + 3] & 0xFFFFu;
                            std::uint64_t H = 1469598103934665603ull, HS = 1469598103934665603ull;
                            for (std::uint32_t i = 0; i < n; ++i)
                            {
                                const std::uint32_t e = C.m_ReadList[First + i];
                                if ((e >> 16) == 0xFFFFu) ++nAllSeen;
                                H  = (H ^ e) * 1099511628211ull;
                                HS = (HS ^ (e & 0xFFFFu)) * 1099511628211ull;
                            }
                            ++nParts; nEnt += n;
                            if (Lists.insert(H ^ n).second) { ++nUnique; nUniqueEnt += n; }
                            if (Sets.insert(HS ^ n).second) { ++nUniqueSet; nUniqueSetEnt += n; }
                        }
                    }
                }
                Progress(std::format("lists: {} parts, {} entries ({:.1f} a part), {:.0f}% all-seen; {} distinct lists ({:.0f}% of the entries), {} distinct surfel sets ({:.0f}% of the entries)",
                    nParts, nEnt, double(nEnt) / double(std::max<std::size_t>(nParts, 1)), 100.0 * double(nAllSeen) / double(std::max<std::size_t>(nEnt, 1)),
                    nUnique, 100.0 * double(nUniqueEnt) / double(std::max<std::size_t>(nEnt, 1)), nUniqueSet, 100.0 * double(nUniqueSetEnt) / double(std::max<std::size_t>(nEnt, 1))));
            }
            if (F.m_nSharedVerts)
            {
                LogMessage(xresource_pipeline::msg_type::WARNING, std::format("GI turned OFF for this mesh: {} vertices belong to clusters of two sections", F.m_nSharedVerts));
                m_FinalGeom.m_GIMode = xgeom_static::gi_mode::OFF;
                return;
            }
            const auto ToSurfel = [](const gi::surfel& S, std::uint32_t iParent) { return geom::gi_surfel{ { S.m_Center.m_X, S.m_Center.m_Y, S.m_Center.m_Z }, S.m_Radius, { S.m_Normal.m_X, S.m_Normal.m_Y, S.m_Normal.m_Z }, iParent }; };
            m_FinalGeom.m_nGISurfels = std::uint32_t(F.m_Fine.size());
            m_FinalGeom.m_nGIParents = std::uint32_t(F.m_Parents.size());
            m_FinalGeom.m_pGISurfel  = new geom::gi_surfel[F.m_Fine.size() + F.m_Parents.size()];
            for (std::size_t i = 0; i < F.m_Fine.size(); ++i) m_FinalGeom.m_pGISurfel[i] = ToSurfel(F.m_Fine[i], F.m_ParentOf[i]);
            for (std::size_t i = 0; i < F.m_Parents.size(); ++i) m_FinalGeom.m_pGISurfel[F.m_Fine.size() + i] = ToSurfel(F.m_Parents[i], 0);
            m_FinalGeom.m_pGISurfelMaterial = new geom::gi_surfel_material[F.m_Fine.size()];
            for (std::size_t i = 0; i < F.m_Materials.size(); ++i) m_FinalGeom.m_pGISurfelMaterial[i] = { F.m_Materials[i].m_U, F.m_Materials[i].m_V, F.m_Materials[i].m_UVRadius, F.m_Materials[i].m_iMaterial };
            m_FinalGeom.m_nGIDerived = std::uint32_t(F.m_Derived.size());
            m_FinalGeom.m_pGIDerived = new std::uint32_t[F.m_Derived.size()];
            std::copy(F.m_Derived.begin(), F.m_Derived.end(), m_FinalGeom.m_pGIDerived);
            m_FinalGeom.m_pGIVertSurfel = new std::uint16_t[F.m_VertSurfels.size()];
            std::copy(F.m_VertSurfels.begin(), F.m_VertSurfels.end(), m_FinalGeom.m_pGIVertSurfel);
            m_FinalGeom.m_nGISections = std::uint32_t(F.m_Sections.size());
            m_FinalGeom.m_pGISection = new geom::gi_section[F.m_Sections.size()];
            for (std::size_t k = 0; k < F.m_Sections.size(); ++k)
            {
                const auto& S = F.m_Sections[k];
                m_FinalGeom.m_pGISection[k] = { S.m_iFine, S.m_nFine, S.m_nOwned, S.m_iParent, S.m_nParents, S.m_iDerived, S.m_nDerived, S.m_iGhost, S.m_nGhosts };
            }
            m_FinalGeom.m_pGIClusterSection = new std::uint16_t[m_FinalGeom.m_nClusters];
            for (std::size_t c = 0; c < m_FinalGeom.m_nClusters; ++c) m_FinalGeom.m_pGIClusterSection[c] = c < F.m_ClusterSection.size() ? F.m_ClusterSection[c] : 0;
            m_FinalGeom.m_nGIGhosts = std::uint32_t(F.m_Ghosts.size());
            m_FinalGeom.m_pGIGhost = new geom::gi_ghost[F.m_Ghosts.size()];
            for (std::size_t g = 0; g < F.m_Ghosts.size(); ++g) m_FinalGeom.m_pGIGhost[g] = { F.m_Ghosts[g].m_Chunk, F.m_Ghosts[g].m_Local };
            LogMessage(xresource_pipeline::msg_type::INFO, std::format("GI in sections: {} surfels ({} parents, {} stored in the sections) cut into {} sections (owned {}..{}, {} split again), {} ghosts ({:.0f}% of the owned), the largest {} local surfels; {:.1f} s",
                M.m_Levels[0].size(), M.m_Levels[1].size(), F.m_Parents.size(), Rep.m_nChunks, Rep.m_OwnedMin, Rep.m_OwnedMax, Rep.m_nSplitsAgain, Rep.m_GhostsTotal, 100.0 * double(Rep.m_GhostsTotal) / double(std::max<std::size_t>(M.m_Levels[0].size(), 1)),
                Rep.m_LocalMax, std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count()));
        }

        void BakeGI()
        {
            namespace gi = xlionrender::gi;
            m_FinalGeom.m_GIMode = m_bGITooBig ? xgeom_static::gi_mode::OFF : m_Descriptor.m_GI.m_Mode;
            if (m_FinalGeom.m_GIMode == xgeom_static::gi_mode::OFF) return;

            gi::surfel_mesh M;
            gi::FromXGeomStatic(M, m_FinalGeom);
            const float R = std::max(m_Descriptor.m_GI.m_SurfelSize, 0.01f);

            // Over the limit the bake stops there (PrecheckGI counted on the mesh before its clean up and quantization: the bake's can differ a little)
            const auto TooMany = [&]
            {
                if (M.m_Levels[0].size() <= max_gi_surfels_v) return false;
                LogMessage(xresource_pipeline::msg_type::WARNING, std::format("GI turned OFF for this mesh (too many surfels, found in the bake): at SurfelSize {} the bake passed {} surfels, the most one mesh can have, and was stopped there. Set GI SurfelSize to {:g} or more to keep GI", R, max_gi_surfels_v, SuggestSurfelSize(R * 1.25f)));
                m_FinalGeom.m_GIMode = xgeom_static::gi_mode::OFF;
                return true;
            };

            M.m_bLeakProtection = m_Descriptor.m_GI.m_bLeakProtection;
            if (m_bGISections || std::getenv("XLION_GI_FORCE_SECTIONS")) { BakeGISections(M, R); return; }      // (the env: tests and measurements on small meshes)
            M.m_OnBakeProgress = [this](float f) { displayProgressBar("Baking the GI surfels", 0.9f * f); };       // (then the read grid and coverage)
            M.Bake(R, 3 * R, {}, max_gi_surfels_v);
            M.m_OnBakeProgress = nullptr;
            if (TooMany()) return;
            {
                // The per-pixel read's coverage (what the renderer builds at load): reported, with the first holes
                M.BuildReadGrid();
                // Defects: seen pieces of surface no surfel serves (thin faces beside other geometry). Listed for the artist; patched with a
                // surfel each when the descriptor says so (the bake again, the same darts plus those: the same surfels, theirs after them)
                if (!M.m_EmptyParts.empty())
                {
                    std::string Where;
                    for (std::size_t i = 0; i < M.m_EmptyParts.size() && i < 8; ++i)
                    {
                        const auto& [P, N] = M.m_EmptyParts[i];
                        Where += std::format(" [{:.2f},{:.2f},{:.2f} facing {:.2f},{:.2f},{:.2f}]", P.m_X, P.m_Y, P.m_Z, N.m_X, N.m_Y, N.m_Z);
                    }
                    LogMessage(xresource_pipeline::msg_type::WARNING, std::format("GI defects: {} pieces of surface no surfel serves (thin faces beside other geometry: fix the model there; the COVERAGE view shows them magenta, or yellow when patched). First:{}", M.m_EmptyParts.size(), Where));
                    if (m_Descriptor.m_GI.m_bPatchDefects)
                    {
                        const auto Forced = M.m_EmptyParts;
                        M.Bake(R, 3 * R, Forced, max_gi_surfels_v);
                        if (TooMany()) return;
                        M.BuildReadGrid();
                        LogMessage(xresource_pipeline::msg_type::INFO, std::format("GI defects patched (PatchDefects): {} surfels added; {} pieces still unserved"
                                  , M.m_FirstPatch == 0xFFFFFFFFu ? 0 : M.m_Levels[0].size() - M.m_FirstPatch, M.m_EmptyParts.size()));
                    }
                }
                const auto Cov = M.Coverage(4);
                LogMessage(Cov.m_nUncovered ? xresource_pipeline::msg_type::WARNING : xresource_pipeline::msg_type::INFO
                          , std::format("GI coverage: {} of {} surface points in sight read no surfel ({:.2f}%; {} covered points not counted); a read lists {:.1f} surfels on average, at most {}", Cov.m_nUncovered, Cov.m_nPoints, 100.0 * Cov.m_nUncovered / std::max(Cov.m_nPoints, 1), Cov.m_nHidden, Cov.m_ListSum / std::max(Cov.m_nPoints, 1), Cov.m_ListMax));
                for (const auto& E : Cov.m_Examples)
                    LogMessage(xresource_pipeline::msg_type::INFO, std::format("GI hole at {:.2f},{:.2f},{:.2f} normal {:.2f},{:.2f},{:.2f}: {} listed, nearest compatible surfel {:.2f}, nearest any {:.2f}"
                              , E.m_P.m_X, E.m_P.m_Y, E.m_P.m_Z, E.m_N.m_X, E.m_N.m_Y, E.m_N.m_Z, E.m_nList, E.m_NearestCompatible, E.m_NearestAny));
            }
            if (M.m_Levels[0].size() > max_gi_surfels_v)
            {
                LogMessage(xresource_pipeline::msg_type::WARNING, std::format("GI turned OFF for this mesh (too many surfels, found after the bake): {} surfels at SurfelSize {}, more than one mesh can have ({}). Set GI SurfelSize to {:g} or more to keep GI", M.m_Levels[0].size(), R, max_gi_surfels_v, SuggestSurfelSize(R * std::sqrt(float(M.m_Levels[0].size()) / max_gi_surfels_v) * 1.05f)));
                return;
            }

            const auto ToSurfel = [](const gi::surfel& S, int iParent)
            {
                return geom::gi_surfel{ { S.m_Center.m_X, S.m_Center.m_Y, S.m_Center.m_Z }, S.m_Radius, { S.m_Normal.m_X, S.m_Normal.m_Y, S.m_Normal.m_Z }, std::uint32_t(iParent) };
            };
            m_FinalGeom.m_nGISurfels = std::uint32_t(M.m_Levels[0].size());
            m_FinalGeom.m_nGIParents = std::uint32_t(M.m_Levels[1].size());
            m_FinalGeom.m_pGISurfel  = new geom::gi_surfel[m_FinalGeom.m_nGISurfels + m_FinalGeom.m_nGIParents];
            for (std::size_t i = 0; i < M.m_Levels[0].size(); ++i) m_FinalGeom.m_pGISurfel[i] = ToSurfel(M.m_Levels[0][i], M.m_ParentOf[i]);
            for (std::size_t i = 0; i < M.m_Levels[1].size(); ++i) m_FinalGeom.m_pGISurfel[m_FinalGeom.m_nGISurfels + i] = ToSurfel(M.m_Levels[1][i], 0);
            m_FinalGeom.m_pGISurfelMaterial = new geom::gi_surfel_material[m_FinalGeom.m_nGISurfels];
            for (std::size_t i = 0; i < M.m_SurfelMaterial.size() && i < m_FinalGeom.m_nGISurfels; ++i)
            {
                const auto& S = M.m_SurfelMaterial[i];
                m_FinalGeom.m_pGISurfelMaterial[i] = { S.m_U, S.m_V, S.m_UVRadius, S.m_iMaterial };
            }
            // What the renderer would build at load (half a minute in a Debug editor for a big geometry): the read grid (built above) and
            // the neighbors (Bake's), packed
            {
                const auto Derived = M.PackDerived();
                m_FinalGeom.m_nGIDerived = std::uint32_t(Derived.size());
                m_FinalGeom.m_pGIDerived = new std::uint32_t[Derived.size()];
                std::copy(Derived.begin(), Derived.end(), m_FinalGeom.m_pGIDerived);
            }
            m_FinalGeom.m_pGIVertSurfel = new std::uint16_t[4 * std::size_t(m_FinalGeom.m_nVertices)];
            for (std::size_t v = 0; v < m_FinalGeom.m_nVertices; ++v)
                for (int k = 0; k < 4; ++k) m_FinalGeom.m_pGIVertSurfel[4 * v + k] = M.m_VertSurfels[v][k] < 0 ? std::uint16_t(0xFFFF) : std::uint16_t(M.m_VertSurfels[v][k]);

            LogMessage(xresource_pipeline::msg_type::INFO, std::format("GI: {} surfels, {} parents, {} vertices", m_FinalGeom.m_nGISurfels, m_FinalGeom.m_nGIParents, m_FinalGeom.m_nVertices));
        }

        //--------------------------------------------------------------------------------------

        xerr Compile()
        {
            try
            {
                //
                // First merge with the latest version of details
                //
                auto ErrorList = m_Descriptor.MergeWithDetails(m_Details, m_ResourceGuid);
                if (not ErrorList.empty())
                {
                    for (auto& e : ErrorList)
                        LogMessage( xresource_pipeline::msg_type::INFO, std::move(e) );
                }

                //
                // Merge meshes
                //
                {
                    displayProgressBar("Merging Meshes", 1);
                    MergeMeshes();
                    displayProgressBar("Merging Meshes", 0);
                }

                //
                // Pre-Transform the verts
                //
                if (   m_Descriptor.m_PreTranslation.m_Scale       != xmath::fvec3::fromOne() 
                    || m_UnitScale                                 != 1
                    || m_Descriptor.m_PreTranslation.m_Translation != xmath::fvec3::fromZero()
                    || m_Descriptor.m_PreTranslation.m_Rotation    != xmath::fvec3::fromZero() )
                {
                    displayProgressBar("PreTranslatingMeshes", 0);

                    xmath::radian3 Rot;
                    Rot.m_Roll  = xmath::radian{ xmath::DegToRad(m_Descriptor.m_PreTranslation.m_Rotation.m_Z) };
                    Rot.m_Pitch = xmath::radian{ xmath::DegToRad(m_Descriptor.m_PreTranslation.m_Rotation.m_X) };
                    Rot.m_Yaw   = xmath::radian{ xmath::DegToRad(m_Descriptor.m_PreTranslation.m_Rotation.m_Y) };

                    const xmath::fmat4 M(m_Descriptor.m_PreTranslation.m_Scale * m_UnitScale, Rot, m_Descriptor.m_PreTranslation.m_Translation);
                    const xmath::fmat3 M2(Rot);
                    for (auto& E : m_RawGeom.m_Vertex )
                    {
                        E.m_Position = M * E.m_Position;

                        E.m_BTN[0].m_Normal   = M2 * E.m_BTN[0].m_Normal;
                        E.m_BTN[0].m_Binormal = M2 * E.m_BTN[0].m_Binormal;
                        E.m_BTN[0].m_Tangent  = M2 * E.m_BTN[0].m_Tangent;
                    }

                    displayProgressBar("PreTranslatingMeshes", 1);
                }

                //
                // Normalize Mesh
                //
                {
                    // Force every vert to have a single UV, normals, etc...
                    for (auto& V : m_RawGeom.m_Vertex)
                    {
                        V.m_nUVs        = 1;
                        assert(V.m_nNormals    == 1);
                        assert(V.m_nTangents   == 1);
                        assert(V.m_nBinormals  == 1);
                        V.m_nColors     = 0;
                    }
                }

                //
                // GI: vertices where they are sparser than the surfels (before the tangents: they are computed for the new ones too)
                //
                std::optional<xraw3d::geom> Unsplit;                    // the mesh as it was, for when the GI does not fit
                if (m_Descriptor.m_GI.m_Mode != xgeom_static::gi_mode::OFF && m_Descriptor.m_GI.m_Density == xgeom_static::gi_density::SPLIT_TRIANGLES)
                {
                    Unsplit = m_RawGeom;
                    displayProgressBar("Splitting triangles for the GI", 0);
                    const auto nFacets = m_RawGeom.m_Facet.size();
                    const auto nAdded  = SplitTrianglesForGI(std::max(m_Descriptor.m_GI.m_SurfelSize, 0.01f), std::max<std::size_t>(nFacets * 16, 1u << 20));
                    LogMessage(xresource_pipeline::msg_type::INFO, std::format("GI: {} triangles split into {}", nFacets, nFacets + nAdded));
                    displayProgressBar("Splitting triangles for the GI", 1);
                }

                //
                // GI: whether the bake fits, before anything else is made of the mesh (the bake is the longest part of a compile)
                //
                displayProgressBar("Checking the GI surfels", 0);
                PrecheckGI();
                if (m_bGITooBig && Unsplit) m_RawGeom = std::move(*Unsplit);      // no GI: its triangles as they were
                Unsplit.reset();
                displayProgressBar("Checking the GI surfels", 1);

                //
                // Clean the mesh
                //
                {
                    displayProgressBar("Cleaning up Geom", 1);
                    m_RawGeom.ComputeTangentsAndBinormalsMikk();
                    m_RawGeom.CleanMesh();
                    m_RawGeom.SortFacetsByMeshMaterialBone();
                    displayProgressBar("Cleaning up Geom", 1);
                }

                //
                // Convert to mesh
                //
                m_FinalGeom.Initialize();

                displayProgressBar("Generating LODs", 1);
                ConvertToCompilerMesh();
                GenenateLODs();
                displayProgressBar("Generating LODs", 0);

                //
                // Generate final mesh
                //
                displayProgressBar("Generating Final Mesh", 0);

                // mm accuracy
                ConvertToGeom(0.001f);

                displayProgressBar("Baking the GI surfels", 0);
                BakeGI();
                displayProgressBar("Baking the GI surfels", 1);
                
                displayProgressBar("Generating Final Mesh", 1);
            }
            catch (std::runtime_error Error )
            {
                LogMessage(xresource_pipeline::msg_type::ERROR, std::format("{}", Error.what()));
                return xerr::create_f<state, "Exception thrown">();
            }

            return {};
        }

        //--------------------------------------------------------------------------------------

        void ComputeDetailStructure()
        {
            // Collect all the meshes
            m_Details.m_MeshList.resize(m_RawGeom.m_Mesh.size());
            for ( auto& M : m_RawGeom.m_Mesh )
            {
                const auto  index   = static_cast<int>(&M - m_RawGeom.m_Mesh.data());
                auto&       DM      = m_Details.m_MeshList[index];

                DM.m_Name           = M.m_Name;
                DM.m_NumFaces       = 0;
                DM.m_NumUVs         = 0;
                DM.m_NumColors      = 0;
                DM.m_NumNormals     = 0;
                DM.m_NumTangents    = 0;
            }

            std::vector<std::vector<int>> MeshMatUsage;

            // prepare to count how many material per mesh we use
            MeshMatUsage.resize(m_RawGeom.m_Mesh.size());
            for (auto& M : m_RawGeom.m_Mesh)
            {
                const auto  index = static_cast<int>(&M - m_RawGeom.m_Mesh.data());
                MeshMatUsage[index].resize(m_RawGeom.m_MaterialInstance.size());
                for (auto& I : MeshMatUsage[index]) I = 0;
            }

            // Add all the faces
            for( auto& F : m_RawGeom.m_Facet)
            {
                const auto  index = static_cast<int>(&F - m_RawGeom.m_Facet.data());
                m_Details.m_MeshList[F.m_iMesh].m_NumFaces++;

                for ( int i=0; i<F.m_nVertices; ++i)
                {
                    m_Details.m_MeshList[F.m_iMesh].m_NumColors   = std::max(m_Details.m_MeshList[F.m_iMesh].m_NumColors,   m_RawGeom.m_Vertex[F.m_iVertex[0]].m_nColors );
                    m_Details.m_MeshList[F.m_iMesh].m_NumUVs      = std::max(m_Details.m_MeshList[F.m_iMesh].m_NumUVs,      m_RawGeom.m_Vertex[F.m_iVertex[0]].m_nUVs    );
                    m_Details.m_MeshList[F.m_iMesh].m_NumNormals  = std::max(m_Details.m_MeshList[F.m_iMesh].m_NumNormals,  m_RawGeom.m_Vertex[F.m_iVertex[0]].m_nNormals);
                    m_Details.m_MeshList[F.m_iMesh].m_NumTangents = std::max(m_Details.m_MeshList[F.m_iMesh].m_NumTangents, m_RawGeom.m_Vertex[F.m_iVertex[0]].m_nTangents);
                }

                MeshMatUsage[F.m_iMesh][F.m_iMaterialInstance]++;
            }

            // Set the material instance counts...
            for ( auto& M : m_Details.m_MeshList)
            {
                const auto  index = static_cast<int>(&M - m_Details.m_MeshList.data());

                // Set all the materials used by this mesh
                for (auto& E : MeshMatUsage[index])
                {
                    if ( E == 0 ) continue;

                    const auto  iMat = static_cast<int>(&E - MeshMatUsage[index].data());
                    M.m_MaterialList.push_back(iMat);
                }
            }

            // collect all the nodes
            std::function<void(xgeom_static::details::node&, const xraw3d::assimp_v3::node&)> CollectNodes = [&](xgeom_static::details::node& FinalNode, const xraw3d::assimp_v3::node& Node )
            {
                FinalNode.m_Name     = Node.m_Name;
                FinalNode.m_MeshList.reserve(Node.m_MeshList.size());

                for ( auto& E : Node.m_MeshList ) 
                {
                    assert( E < m_Details.m_MeshList.size() );
                    FinalNode.m_MeshList.emplace_back(static_cast<int>(E));
                }

                for ( auto& E : Node.m_Children ) CollectNodes( FinalNode.m_Children.emplace_back(), E);
            };
            CollectNodes(m_Details.m_RootNode, m_RootNode);

            // Set all the materials
            for (auto& E : m_RawGeom.m_MaterialInstance)
            {
                m_Details.m_MaterialList.emplace_back(E.m_Name);
            }

            // Find total faces
            m_Details.m_NumFaces        = 0;
            for (auto& M : m_Details.m_MeshList)
            {
                m_Details.m_NumFaces += M.m_NumFaces;
            }
        }

        //--------------------------------------------------------------------------------------

        xerr onCompile(void) noexcept override
        {
            //
            // Read the descriptor file...
            //
            displayProgressBar("Loading Descriptor", 0);
            {
                xproperty::settings::context    Context{};
                auto                            DescriptorFileName = std::format(L"{}/{}/Descriptor.txt", m_ProjectPaths.m_Project, m_InputSrcDescriptorPath);

                if ( auto Err = m_Descriptor.Serialize(true, DescriptorFileName, Context); Err)
                    return Err;
            }

            //
            // Do a quick validation of the descriptor
            //
            {
                std::vector<std::string> Errors;
                m_Descriptor.Validate(Errors);
                if (not Errors.empty())
                {
                    for( auto& E : Errors)
                        LogMessage(xresource_pipeline::msg_type::ERROR, std::move(E) );

                    return xerr::create_f<state, "Validation Errors">();
                }
            }
            displayProgressBar("Loading Descriptor", 1);

            //
            // Register the raw FBX/OBJ as a dependency BEFORE attempting to load it - so the Asset
            // Browser's "who uses this file" tracking (m_AssetDataBase) knows about the link even if
            // the load below fails (a broken link should still show as a tracked dependency, not
            // silently vanish from the graph). Mirrors xtexture_compiler.cpp's own
            // DumpAllFileNamesIntoHash()/AddTexture, which registers its own raw inputs the same way,
            // before loading them - this compiler was simply missing the equivalent call entirely,
            // confirmed live: a real StaticGeom resource's own dependencies.txt didn't exist at all,
            // and the Asset Tree's hover tooltip correctly reported zero dependents for its source FBX
            // even though a real StaticGeom resource was compiled from it.
            m_Dependencies.m_Assets.push_back(m_Descriptor.m_ImportAsset);

            //
            // Load the source data
            //
            displayProgressBar("Loading Mesh", 0);
            if ( auto Err = LoadRaw(std::format(L"{}/{}", m_ProjectPaths.m_Project, m_Descriptor.m_ImportAsset)); Err )
                return Err;
            displayProgressBar("Loading Mesh", 1);

            // AutoUnits (a new descriptor): the source to meters, by what its file says or a guess from its size (a level is up to 1000 m)
            if (m_Descriptor.m_PreTranslation.m_bAutoUnits)
            {
                const auto Units = xraw3d::assimp_v3::ResolveUnits(m_Units, m_RawGeom.m_Vertex, 1000.0f);
                m_UnitScale = Units.m_MetersPerUnit;
                LogMessage(xresource_pipeline::msg_type::INFO, std::format("AutoUnits: {}", Units.m_Reason));
            }

            //
            // Fill the detail structure
            //
            ComputeDetailStructure();

            //
            // The materials of the model, as virtual Material Instances and Textures
            //
            if (m_Descriptor.m_ImportMaterials.m_bImport)
            {
                displayProgressBar("Importing Materials", 0);
                xmaterial_instance::import::emitter Emitter
                { .m_ProjectPath        = m_ProjectPaths.m_Project
                , .m_Descriptors        = m_ProjectPaths.m_Descriptors
                , .m_OwnerFolder        = std::format(L"{}/{}.desc", m_ProjectPaths.m_Descriptors, m_ResourcePartialPath)
                , .m_Owner              = { m_ResourceGuid, xgeom_static::resource_type_guid_v }
                , .m_ModelPath          = m_Descriptor.m_ImportAsset
                , .m_Settings           = m_Descriptor.m_ImportMaterials
                , .m_Dependencies       = m_Dependencies
                , .m_Warning            = [&](std::string Text) { LogMessage(xresource_pipeline::msg_type::WARNING, std::move(Text)); }
                , .m_Info               = [&](std::string Text) { LogMessage(xresource_pipeline::msg_type::INFO, std::move(Text)); }
                };
                Emitter.Emit(m_RawGeom.m_MaterialInstance, m_RawGeom.m_Facet);
                displayProgressBar("Importing Materials", 1);
            }

            //
            // OK Time to compile
            //
            Compile();

            //
            // Serialize the details structure
            //
            {
                xtextfile::stream File;
                if (auto Err = File.Open(false, std::format(L"{}\\Details.txt", m_ResourceLogPath), xtextfile::file_type::TEXT); Err)
                    return xerr::create_f<state, "Failed while opening the details.txt so it can't be saved">(Err);

                xproperty::settings::context C{};
                // A tree too deep for xproperty's paths is saved apart, in its own table after the properties
                m_Details.m_bCompactTree = xgeom_static::details::Depth(m_Details.m_RootNode) > xgeom_static::details::compact_depth_v;
                xgeom_static::details::node Tree;
                if (m_Details.m_bCompactTree) std::swap(Tree, m_Details.m_RootNode);
                auto Err = xproperty::sprop::serializer::Stream( File, m_Details, C);
                if (m_Details.m_bCompactTree) std::swap(Tree, m_Details.m_RootNode);
                if ( Err )
                    return xerr::create_f<state, "Failed while serializing details.txt">(Err);
                if ( m_Details.m_bCompactTree )
                    if ( auto Err2 = m_Details.StreamCompactTree(File, m_Details.m_RootNode); Err2 )
                        return xerr::create_f<state, "Failed while serializing the node tree of details.txt">(Err2);
            }

            //
            // Export
            //
            int Count = 0;
            for (auto& T : m_Target)
            {
                displayProgressBar("Serializing", Count++ / (float)m_Target.size());

                if (T.m_bValid)
                {
                    Serialize(T.m_DataPath);
                }
            }
            displayProgressBar("Serializing", 1);
            return {};
        }

        //--------------------------------------------------------------------------------------

        void Serialize(const std::wstring_view FilePath)
        {
            xserializer::stream Serializer;
            if( auto Err = Serializer.Save
                ( FilePath
                , m_FinalGeom
                , m_OptimizationType == optimization_type::O0 ? xserializer::compression_level::FAST : m_OptimizationType == optimization_type::O1 ? xserializer::compression_level::MEDIUM : xserializer::compression_level::HIGH
                ); Err )
            {
                throw(std::runtime_error(std::string(Err.getMessage())));
            }
        }

        meshopt_VertexCacheStatistics   m_VertCacheAMDStats;
        meshopt_VertexCacheStatistics   m_VertCacheNVidiaStats;
        meshopt_VertexCacheStatistics   m_VertCacheIntelStats;
        meshopt_VertexCacheStatistics   m_VertCacheStats;
        meshopt_VertexFetchStatistics   m_VertFetchStats;
        meshopt_OverdrawStatistics      m_OverdrawStats;
        std::unordered_map<std::string, xgeom_static::mesh_details*> m_NameToDetail;
        xgeom_static::details           m_Details;
        xgeom_static::descriptor        m_Descriptor;

        xgeom_static::geom              m_FinalGeom;
        std::vector<mesh>               m_CompilerMesh;
        xraw3d::geom                    m_RawGeom;
        bool                            m_bGITooBig     { false };      // PrecheckGI: the bake would not fit, no GI this compile
        bool                            m_bGISections   { false };      // PrecheckGI: over one GI mesh's cap: BakeGI cuts it into sections
        std::vector<gi_region_node>     m_GIRegions;                    // ... their regions (BuildGIRegions)
        int                             m_nGIRegions    { 0 };
        std::vector<int>                m_ClusterRegion;                // ConvertToGeom: per cluster its region (the sections are these)
        xraw3d::assimp_v3::node         m_RootNode;
        xraw3d::assimp_v3::units        m_Units;                        // of the source file (LoadRaw)
        float                           m_UnitScale     { 1 };          // AutoUnits: the source to meters, before the PreTranslation
    };

    //------------------------------------------------------------------------------------

    std::unique_ptr<instance> instance::Create(void)
    {
        return std::make_unique<implementation>();
    }
}


