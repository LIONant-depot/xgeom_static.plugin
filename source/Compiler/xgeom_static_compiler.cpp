#include "xgeom_static_compiler.h"

#include <dependencies/xcontainer/source/xcontainer_lockless_pool.h>

#include "dependencies/xraw3d/source/xraw3d.h"
#include "dependencies/xraw3d/source/details/xraw3d_assimp_import_v2.h"

#include "dependencies/meshoptimizer/src/meshoptimizer.h"
#include "dependencies/xbitmap/source/xcolor.h"
#include "dependencies/xbits/source/xbits.h"

#include "../xgeom_static_rsc_descriptor.h"
#include "../xgeom_static.h"
#include "../xgeom_static_details.h"

#include "dependencies/xproperty/source/xcore/my_properties.cpp"

namespace xgeom_compiler
{
    struct implementation : xgeom_compiler::instance
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
            std::vector<std::uint32_t>      m_Indices;
            std::vector<lod>                m_LODs;
            std::uint32_t                   m_iMaterial;
            int                             m_nWeights      { 0 };
            int                             m_nUVs          { 0 };
            bool                            m_bHasColor     { false };
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
            xraw3d::assimp_v2::importer Importer;
            Importer.m_Settings.m_bAnimated = false;
            if ( auto Err = Importer.Import(Path, m_RawGeom); Err )
                return xerr::create_f<state, "Failed to import the asset">(Err);

            return {};
        }

        //--------------------------------------------------------------------------------------

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

                        if( SubMesh.m_Indices.size() && SubMesh.m_nUVs != 0 && RawVert.m_nUVs < SubMesh.m_nUVs )
                        {
                            printf("WARNING: Found a vertex with an inconsistent set of uvs (Expecting %d, found %d) MeshName: %s \n"
                            , SubMesh.m_nUVs
                            , RawVert.m_nUVs
                            , Mesh.m_Name.data()
                            );
                        }
                        else
                        {
                            SubMesh.m_nUVs = RawVert.m_nUVs;

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

        //--------------------------------------------------------------------------------------

        void GenenateLODs()
        {
            for (auto& M : m_CompilerMesh)
            {
                auto iDescMesh = m_Descriptor.findMesh( M.m_Name );

                if (iDescMesh == -1)
                    continue;

                for (auto& S : M.m_SubMesh)
                {
                    if (m_Descriptor.m_MeshList[iDescMesh].m_LODs.size())
                    {
                        std::size_t IndexCount = S.m_Indices.size();

                        for ( size_t i = 0; i < m_Descriptor.m_MeshList[iDescMesh].m_LODs.size(); ++i)
                        {
                            //const float       threshold               = std::powf(m_Descriptor.m_MeshList[iDescMesh].m_LODs[i].m_LODReduction, float(i));
                            const std::size_t target_index_count      = std::size_t(IndexCount * m_Descriptor.m_MeshList[iDescMesh].m_LODs[i].m_LODReduction + 0.005f) / 3 * 3;
                            const float       target_error            = 1e-2f;
                            const auto&       Source                  = (S.m_LODs.size())? S.m_LODs.back().m_Indices : S.m_Indices;

                            if( Source.size() < target_index_count )
                                break;

                            auto& NewLod = S.m_LODs.emplace_back();

                            NewLod.m_Indices.resize(Source.size());
                            NewLod.m_Indices.resize( meshopt_simplify( NewLod.m_Indices.data(), Source.data(), Source.size(), &S.m_Vertex[0].m_Position.m_X, S.m_Vertex.size(), sizeof(vertex), target_index_count, target_error));

                            // Set the new count
                            IndexCount = NewLod.m_Indices.size();
                        }
                    }
                }
            }
        }

        //--------------------------------------------------------------------------------------

        void optimizeFacesAndVerts()
        {
            for( auto& M : m_CompilerMesh)
            {
                const size_t kCacheSize = 16;
                for( auto& S : M.m_SubMesh )
                {
                    meshopt_optimizeVertexCache ( S.m_Indices.data(), S.m_Indices.data(), S.m_Indices.size(), S.m_Vertex.size() ); 
                    meshopt_optimizeOverdraw    ( S.m_Indices.data(), S.m_Indices.data(), S.m_Indices.size(), &S.m_Vertex[0].m_Position.m_X, S.m_Vertex.size(), sizeof(vertex), 1.0f );

                    for( auto& L : S.m_LODs )
                    {
                        meshopt_optimizeVertexCache ( L.m_Indices.data(), L.m_Indices.data(), L.m_Indices.size(), S.m_Vertex.size() );
                        meshopt_optimizeOverdraw    ( L.m_Indices.data(), L.m_Indices.data(), L.m_Indices.size(), &S.m_Vertex[0].m_Position.m_X, S.m_Vertex.size(), sizeof(vertex), 1.0f );
                    }
                }
            }
        }

        //--------------------------------------------------------------------------------------

        // Helpers
        static xmath::fvec2 oct_wrap(xmath::fvec2 v) noexcept
        {
            return (xmath::fvec2(1.0f) - xmath::fvec2(v.m_Y, v.m_X).Abs()) * (v.Step({0}) * 2.0f - 1.0f);
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

        // Vertex group for palette clustering
        static void RecursePosCluster
        ( const std::vector<vertex>&                    Verts
        ,       std::vector<uint32_t>                   Group
        , const float                                   MaxExtent
        ,       std::vector<geom::pos_palette_entry>&   PosPalettes
        ,       std::vector<int>&                       VertPal
        )
        {
            if (Group.empty()) return;

            BBox3 bb;
            for (uint32_t vi : Group) bb.Update(Verts[vi].m_Position);

            xmath::fvec3 extent = bb.m_MaxPos - bb.m_MinPos;
            float max_e = xmath::Max(xmath::Max(extent.m_X, extent.m_Y), extent.m_Z);

            if (max_e <= MaxExtent) 
            {
                xmath::fvec3 center = (bb.m_MinPos + bb.m_MaxPos) * 0.5f;
                xmath::fvec3 scale  = xmath::fvec3::Max((bb.m_MaxPos - bb.m_MinPos) * 0.5f, xmath::fvec3(1e-6f));

                int pal_id = static_cast<int>(PosPalettes.size());
                if (pal_id > 127) 
                {
                    throw std::runtime_error("Too many position palettes (max 128).");
                }

                geom::pos_palette_entry entry;
                entry.m_Scale.m_X  = scale.m_X;  entry.m_Scale.m_Y  = scale.m_Y;  entry.m_Scale.m_Z  = scale.m_Z;
                entry.m_Offset.m_X = center.m_X; entry.m_Offset.m_Y = center.m_Y; entry.m_Offset.m_Z = center.m_Z;
                PosPalettes.push_back(entry);

                for (uint32_t vi : Group) VertPal[vi] = pal_id;
            }
            else 
            {
                int axis = 0;
                if (extent[1] > extent[axis]) axis = 1;
                if (extent[2] > extent[axis]) axis = 2;

                std::sort(Group.begin(), Group.end(), [&](std::uint32_t a, std::uint32_t b)
                {
                    return Verts[a].m_Position[axis] < Verts[b].m_Position[axis];
                });

                std::size_t half = Group.size() / 2;
                std::vector<std::uint32_t> g1(Group.begin(), Group.begin() + half);
                std::vector<std::uint32_t> g2(Group.begin() + half, Group.end());

                RecursePosCluster(Verts, g1, MaxExtent, PosPalettes, VertPal);
                RecursePosCluster(Verts, g2, MaxExtent, PosPalettes, VertPal);
            }
        }

        //--------------------------------------------------------------------------------------

        static void RecurseUVCluster
        ( const std::vector<vertex>&                 Verts
        ,       std::vector<uint32_t>                Group
        , const float                                MaxExtent
        ,       std::vector<geom::uv_palette_entry>& UVPalettes
        ,       std::vector<int>&                    VertPal
        )
        {
            if (Group.empty()) return;

            BBox2 bb;
            for (uint32_t vi : Group) bb.Update(Verts[vi].m_UVs[0]);  // Use first UV

            xmath::fvec2 extent = bb.m_MaxUV - bb.m_MinUV;
            float max_e = xmath::Max(extent.m_X, extent.m_Y);

            if (max_e <= MaxExtent) 
            {
                xmath::fvec2 uv_scale = xmath::fvec2::Max(bb.m_MaxUV - bb.m_MinUV, xmath::fvec2(1e-6f));

                int pal_id = static_cast<int>(UVPalettes.size());
                if (pal_id > 255) 
                {
                    throw std::runtime_error("Too many UV palettes (max 256).");
                }

                geom::uv_palette_entry entry;
                entry.m_ScaleAndOffset.m_X  = uv_scale.m_X;     entry.m_ScaleAndOffset.m_Y  = uv_scale.m_Y;
                entry.m_ScaleAndOffset.m_Z  = bb.m_MinUV.m_X;   entry.m_ScaleAndOffset.m_W = bb.m_MinUV.m_Y;
                UVPalettes.push_back(entry);

                for (uint32_t vi : Group) VertPal[vi] = pal_id;
            }
            else 
            {
                int axis = (extent.m_X > extent.m_Y) ? 0 : 1;

                std::sort(Group.begin(), Group.end(), [&](std::uint32_t a, std::uint32_t b)
                {
                    return Verts[a].m_UVs[0][axis] < Verts[b].m_UVs[0][axis];
                });

                std::size_t half = Group.size() / 2;
                std::vector<std::uint32_t> g1(Group.begin(), Group.begin() + half);
                std::vector<std::uint32_t> g2(Group.begin() + half, Group.end());

                RecurseUVCluster(Verts, g1, MaxExtent, UVPalettes, VertPal);
                RecurseUVCluster(Verts, g2, MaxExtent, UVPalettes, VertPal);
            }
        }

        //--------------------------------------------------------------------------------------

        // Split into clusters (GPU chunks)
        static void RecurseClusterSplit
        ( const std::vector<vertex>&                    InputVerts
        , const std::vector<uint32_t>&                  InputIndices
        , const TriCluster&                             c
        , uint32_t                                      MaxVerts
        , const std::vector<int>&                       VertPosPal
        , const std::vector<int>&                       VertUVPal
        , const std::vector<float>&                     BinormalSigns
        , const std::vector<geom::pos_palette_entry>&   PosPalettes
        , const std::vector<geom::uv_palette_entry>&    UVPalettes
        , std::vector<geom::cluster>&                   OutputClusters
        , std::vector<geom::vertex>&                    AllStaticVerts
        , std::vector<geom::vertex_extras>&             AllExtrasVerts
        , std::vector<uint32_t>&                        AllIndices
        )
        {
            if (c.tri_ids.empty()) return;

            std::unordered_set<uint32_t> used_verts;
            for (uint32_t ti : c.tri_ids) 
            {
                for (int j = 0; j < 3; ++j) 
                {
                    used_verts.insert(InputIndices[ti * 3 + j]);
                }
            }

            if (used_verts.size() <= MaxVerts) 
            {
                // Remap verts locally
                std::vector<uint32_t> new_vert_ids(used_verts.begin(), used_verts.end());
                std::sort(new_vert_ids.begin(), new_vert_ids.end());
                std::unordered_map<uint32_t, uint32_t> old_to_new;
                uint32_t new_id = 0;
                for (uint32_t ov : new_vert_ids) 
                {
                    old_to_new[ov] = new_id++;
                }

                // Compute cluster bbox
                BBox3 cluster_bb;
                for (uint32_t ov : new_vert_ids) 
                {
                    cluster_bb.Update(InputVerts[ov].m_Position);
                }

                // Append verts to global
                uint32_t cluster_vert_start = static_cast<uint32_t>(AllStaticVerts.size());
                AllStaticVerts.resize(AllStaticVerts.size() + new_vert_ids.size());
                AllExtrasVerts.resize(AllExtrasVerts.size() + new_vert_ids.size());
                for (uint32_t i = 0; i < new_vert_ids.size(); ++i) 
                {
                    uint32_t        ov          = new_vert_ids[i];
                    const vertex&   v           = InputVerts[ov];
                    int             pos_pal_id  = VertPosPal[ov];
                    int             uv_pal_id   = VertUVPal[ov];
                    float           sign_val    = BinormalSigns[ov];
                    int             sign_bit    = (sign_val < 0.0f ? 1 : 0);

                    // Pos compression
                    const auto&     pos_entry   = PosPalettes[pos_pal_id];
                    const auto      center      = xmath::fvec3(pos_entry.m_Offset.m_X, pos_entry.m_Offset.m_Y, pos_entry.m_Offset.m_Z);
                    const auto      scale       = xmath::fvec3(pos_entry.m_Scale.m_X,  pos_entry.m_Scale.m_Y,  pos_entry.m_Scale.m_Z);
                    const float     nx          = ((v.m_Position.m_X - center.m_X) / scale.m_X + 1.0f) * 32767.5f - 32768.0f;
                    const float     ny          = ((v.m_Position.m_Y - center.m_Y) / scale.m_Y + 1.0f) * 32767.5f - 32768.0f;
                    const float     nz          = ((v.m_Position.m_Z - center.m_Z) / scale.m_Z + 1.0f) * 32767.5f - 32768.0f;
                    geom::vertex&   sv          = AllStaticVerts[cluster_vert_start + i];

                    sv.m_XPos   = static_cast<int16_t>(std::round(nx));
                    sv.m_YPos   = static_cast<int16_t>(std::round(ny));
                    sv.m_ZPos   = static_cast<int16_t>(std::round(nz));
                    sv.m_Extra  = sign_bit | (pos_pal_id << 1) | (uv_pal_id << 8);

                    // UV (first only)
                    const auto&             uv_entry    = UVPalettes[uv_pal_id];
                    const auto              uv_min      = xmath::fvec2(uv_entry.m_ScaleAndOffset.m_Z, uv_entry.m_ScaleAndOffset.m_W);
                    const auto              uv_scale    = xmath::fvec2(uv_entry.m_ScaleAndOffset.m_X, uv_entry.m_ScaleAndOffset.m_Y);
                    const auto              norm_uv     = (v.m_UVs[0] - uv_min) / uv_scale;
                    geom::vertex_extras&    ev          = AllExtrasVerts[cluster_vert_start + i];

                    ev.m_UV[0] = static_cast<uint16_t>(std::round(norm_uv.m_X * 65535.0f));
                    ev.m_UV[1] = static_cast<uint16_t>(std::round(norm_uv.m_Y * 65535.0f));

                    // Oct normal/tangent
                    const auto oct_n = oct_encode(v.m_Normal.NormalizeSafeCopy());
                    ev.m_OctNormal[0] = static_cast<int16_t>(std::round(oct_n.m_X * 32767.0f));
                    ev.m_OctNormal[1] = static_cast<int16_t>(std::round(oct_n.m_Y * 32767.0f));

                    const auto oct_t = oct_encode(v.m_Tangent.NormalizeSafeCopy());
                    ev.m_OctTangent[0] = static_cast<int16_t>(std::round(oct_t.m_X * 32767.0f));
                    ev.m_OctTangent[1] = static_cast<int16_t>(std::round(oct_t.m_Y * 32767.0f));
                }

                // Append indices to global (remapped)
                uint32_t cluster_index_start = static_cast<uint32_t>(AllIndices.size());
                for (uint32_t ti : c.tri_ids) 
                {
                    for (int j = 0; j < 3; ++j) 
                    {
                        uint32_t ov = InputIndices[ti * 3 + j];
                        AllIndices.push_back(old_to_new[ov]);
                    }
                }
            }
            else
            {
                BBox3 bb;
                for (uint32_t ti : c.tri_ids) 
                {
                    for (int j = 0; j < 3; ++j) 
                    {
                        uint32_t vi = InputIndices[ti * 3 + j];
                        bb.Update(InputVerts[vi].m_Position);
                    }
                }
                xmath::fvec3    extent  = bb.m_MaxPos - bb.m_MinPos;
                int             axis    = 0;
                float           max_e   = extent[0];
                if (extent[1] > max_e) { max_e = extent[1]; axis = 1; }
                if (extent[2] > max_e) { max_e = extent[2]; axis = 2; }
                float split_pos = (bb.m_MinPos[axis] + bb.m_MaxPos[axis]) * 0.5f;

                TriCluster c1, c2;
                for (uint32_t ti : c.tri_ids) 
                {
                    xmath::fvec3 cent = (InputVerts[InputIndices[ti * 3 + 0]].m_Position +
                                         InputVerts[InputIndices[ti * 3 + 1]].m_Position +
                                         InputVerts[InputIndices[ti * 3 + 2]].m_Position) / 3.0f;
                    if (cent[axis] < split_pos) c1.tri_ids.push_back(ti);
                    else                        c2.tri_ids.push_back(ti);
                }

                RecurseClusterSplit(InputVerts, InputIndices, c1, MaxVerts, VertPosPal, VertUVPal, BinormalSigns, PosPalettes, UVPalettes, OutputClusters, AllStaticVerts, AllExtrasVerts, AllIndices);
                RecurseClusterSplit(InputVerts, InputIndices, c2, MaxVerts, VertPosPal, VertUVPal, BinormalSigns, PosPalettes, UVPalettes, OutputClusters, AllStaticVerts, AllExtrasVerts, AllIndices);
            }
        }

        //--------------------------------------------------------------------------------------

        void ConvertToGeom(float target_precision)
        {
            const std::vector<mesh>& compiler_meshes = m_CompilerMesh;
            geom& result = m_FinalGeom;

            std::vector<geom::mesh>                 OutMeshes;
            std::vector<geom::lod>                  OutLODs;
            std::vector<geom::submesh>              OutSubmeshes;
            std::vector<geom::cluster>              OutClusters;
            std::vector<geom::pos_palette_entry>    OutPosPalettes;
            std::vector<geom::uv_palette_entry>     OutUVPalettes;
            std::vector<geom::vertex>               OutAllStaticVerts;
            std::vector<geom::vertex_extras>        OutAllExtrasVerts;
            std::vector<uint32_t>                   OutAllIndices;
            BBox3                                   OutGlobalBBox;

            uint16_t current_lod_idx        = 0;
            uint16_t current_submesh_idx    = 0;
            uint16_t current_cluster_idx    = 0;

            float max_extent = target_precision * 65535.0f;

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

                    for (const auto& lod_in : input_sm.m_LODs) 
                    {
                        for (size_t ti = 0; ti < lod_in.m_Indices.size() / 3; ++ti) 
                        {
                            std::uint32_t i1 = lod_in.m_Indices[ti * 3 + 0];
                            std::uint32_t i2 = lod_in.m_Indices[ti * 3 + 1];
                            std::uint32_t i3 = lod_in.m_Indices[ti * 3 + 2];
                            total_edge_len += (input_sm.m_Vertex[i1].m_Position - input_sm.m_Vertex[i2].m_Position).Length();
                            total_edge_len += (input_sm.m_Vertex[i2].m_Position - input_sm.m_Vertex[i3].m_Position).Length();
                            total_edge_len += (input_sm.m_Vertex[i3].m_Position - input_sm.m_Vertex[i1].m_Position).Length();
                            num_edges += 3;
                        }
                    }
                }

                geom::mesh out_m;
                xstrtool::Copy(out_m.m_Name, input_mesh.m_Name);
                out_m.m_Name[31]        = '\0';
                out_m.m_WorldPixelSize  = (num_edges > 0) ? total_edge_len / num_edges : 0.0f;
                out_m.m_BBox            = mesh_bb.to_fbbox();
                out_m.m_nLODs           = static_cast<uint16_t>(input_mesh.m_SubMesh.empty() ? 0 : input_mesh.m_SubMesh[0].m_LODs.size());
                out_m.m_iLOD            = current_lod_idx;
                OutMeshes.push_back(out_m);
                current_lod_idx += out_m.m_nLODs;

                for (size_t lod_level = 0; lod_level < out_m.m_nLODs; ++lod_level) 
                {
                    geom::lod out_l;
                    out_l.m_ScreenArea  = input_mesh.m_SubMesh.empty() ? 0.0f : input_mesh.m_SubMesh[0].m_LODs[lod_level].m_ScreenArea;
                    out_l.m_iSubmesh    = current_submesh_idx;
                    out_l.m_nSubmesh    = static_cast<uint16_t>(input_mesh.m_SubMesh.size());
                    OutLODs.push_back(out_l);
                    current_submesh_idx += out_l.m_nSubmesh;

                    for (const auto& input_sm : input_mesh.m_SubMesh) 
                    {
                        geom::submesh out_sm;
                        out_sm.m_iMaterial  = static_cast<uint16_t>(input_sm.m_iMaterial);
                        out_sm.m_iCluster   = current_cluster_idx;

                        const std::vector<uint32_t>& lod_indices = (lod_level < input_sm.m_LODs.size()) ? input_sm.m_LODs[lod_level].m_Indices : input_sm.m_Indices;

                        std::vector<float> binormal_signs(input_sm.m_Vertex.size(), 1.0f);
                        for (size_t i = 0; i < input_sm.m_Vertex.size(); ++i) 
                        {
                            const vertex& v = input_sm.m_Vertex[i];
                            if (input_sm.m_bHasBTN) 
                            {
                                xmath::fvec3 computed_binormal = xmath::fvec3::Cross(v.m_Normal, v.m_Tangent);
                                float dot_val = xmath::fvec3::Dot(computed_binormal, v.m_Binormal);
                                binormal_signs[i] = (dot_val >= 0.0f) ? 1.0f : -1.0f;
                            }
                        }

                        std::vector<int>        vert_pos_pal(input_sm.m_Vertex.size(), -1);
                        std::vector<uint32_t>   all_local_verts(input_sm.m_Vertex.size());
                        for (uint32_t i = 0; i < input_sm.m_Vertex.size(); ++i) all_local_verts[i] = i;
                        RecursePosCluster(input_sm.m_Vertex, all_local_verts, max_extent, OutPosPalettes, vert_pos_pal);

                        std::vector<int> vert_uv_pal(input_sm.m_Vertex.size(), -1);
                        RecurseUVCluster(input_sm.m_Vertex, all_local_verts, max_extent, OutUVPalettes, vert_uv_pal);

                        TriCluster initial;
                        uint32_t num_tris = static_cast<uint32_t>(lod_indices.size() / 3);
                        initial.tri_ids.resize(num_tris);
                        for (uint32_t i = 0; i < num_tris; ++i) initial.tri_ids[i] = i;

                        size_t prev_num_clusters = OutClusters.size();
                        RecurseClusterSplit(input_sm.m_Vertex, lod_indices, initial, 65534, vert_pos_pal, vert_uv_pal, binormal_signs, OutPosPalettes, OutUVPalettes, OutClusters, OutAllStaticVerts, OutAllExtrasVerts, OutAllIndices);

                        out_sm.m_nCluster = static_cast<uint16_t>(OutClusters.size() - prev_num_clusters);
                        current_cluster_idx += out_sm.m_nCluster;
                        OutSubmeshes.push_back(out_sm);
                    }
                }
            }

            result.m_pMesh          = new geom::mesh[OutMeshes.size()];
            result.m_nMeshes        = static_cast<std::uint16_t>(OutMeshes.size());
            std::copy(OutMeshes.begin(), OutMeshes.end(), result.m_pMesh);

            result.m_pLOD           = new geom::lod[OutLODs.size()];
            result.m_nLODs          = static_cast<std::uint16_t>(OutLODs.size());
            std::copy(OutLODs.begin(), OutLODs.end(), result.m_pLOD);

            result.m_pSubMesh       = new geom::submesh[OutSubmeshes.size()];
            result.m_nSubMeshs      = static_cast<std::uint16_t>(OutSubmeshes.size());
            std::copy(OutSubmeshes.begin(), OutSubmeshes.end(), result.m_pSubMesh);

            result.m_pCluster       = new geom::cluster[OutClusters.size()];
            result.m_nClusters      = static_cast<std::uint16_t>(OutClusters.size());
            std::copy(OutClusters.begin(), OutClusters.end(), result.m_pCluster);

            result.m_nDefaultMaterialInstances = 0;
            result.m_pDefaultMaterialInstances = nullptr;

            result.m_BBox           = OutGlobalBBox.to_fbbox();
            result.m_nPosPalette    = static_cast<std::uint8_t>(OutPosPalettes.size());
            result.m_nUVPalette     = static_cast<std::uint8_t>(OutUVPalettes.size());
            result.m_nVertices      = static_cast<std::uint32_t>(OutAllStaticVerts.size());
            result.m_nIndices       = static_cast<std::uint32_t>(OutAllIndices.size());

            // Compute aligned sizes for GPU data
            auto align = [](size_t offset, size_t alignment) -> size_t
            {
                return (offset + alignment - 1) & ~(alignment - 1);
            };

            constexpr std::size_t vulkan_align      = 16;  // Min for Vulkan buffers/UBO
            const     std::size_t PosPalSize        = OutPosPalettes.size()     * sizeof(geom::pos_palette_entry);
            const     std::size_t UVPalSize         = OutUVPalettes.size()      * sizeof(geom::uv_palette_entry);
            const     std::size_t VertexSize        = OutAllStaticVerts.size()  * sizeof(geom::vertex);
            const     std::size_t ExtrasSize        = OutAllExtrasVerts.size()  * sizeof(geom::vertex_extras);
            const     std::size_t IndicesSize       = OutAllIndices.size()      * sizeof(std::uint16_t);
            std::size_t current_offset   = 0;

            result.m_PosPalettesOffset  = current_offset;

            current_offset              = align(current_offset + PosPalSize, vulkan_align);
            result.m_UVPalettesOffset   = current_offset;

            current_offset              = align(current_offset + UVPalSize, vulkan_align);
            result.m_VertexOffset       = current_offset;

            current_offset              = align(current_offset + VertexSize, vulkan_align);
            result.m_VertexExtrasOffset = current_offset;

            current_offset              = align(current_offset + ExtrasSize, vulkan_align);
            result.m_IndicesOffset      = current_offset;

            // Now we can do the full allocation for everything
            current_offset              = align(current_offset + IndicesSize, vulkan_align);
            result.m_DataSize           = current_offset;
            result.m_pData              = new char[result.m_DataSize];

            // Copy data into m_pData
            std::memcpy(result.m_pData + result.m_PosPalettesOffset,    OutPosPalettes.data(),      PosPalSize);
            std::memcpy(result.m_pData + result.m_UVPalettesOffset,     OutUVPalettes.data(),       UVPalSize);
            std::memcpy(result.m_pData + result.m_VertexOffset,         OutAllStaticVerts.data(),   VertexSize);
            std::memcpy(result.m_pData + result.m_VertexExtrasOffset,   OutAllExtrasVerts.data(),   ExtrasSize);
            char* indices_ptr = result.m_pData + result.m_IndicesOffset;
            for (size_t i = 0; i < OutAllIndices.size(); ++i) 
            {
                assert(OutAllIndices[i] < 0xffff);
                reinterpret_cast<std::uint16_t*>(indices_ptr)[i] = static_cast<std::uint16_t>(OutAllIndices[i]);
            }
        }

        //--------------------------------------------------------------------------------------

        void MergeMeshes()
        {
            //
            // Check if we actually have any work to do...
            //
            int iMergedMesh = -1;
            for (auto E : m_Descriptor.m_MeshList)
            {
                if (E.m_bMerge == false)
                {
                    iMergedMesh = 0;
                    break;
                }
            }

            if (not m_Descriptor.m_MeshList.empty() && iMergedMesh == -1)
            {
                LogMessage(xresource_pipeline::msg_type::WARNING, std::format("You have mark all meshes as NO MERGE, so I am unable to merge anything..."));
                return;
            }

            std::vector<int> MeshesMerge;
            std::vector<int> RemapMeshes;

            // Allocate and clear the list
            MeshesMerge.resize(m_RawGeom.m_Mesh.size());
            RemapMeshes.resize(MeshesMerge.size());
            for (auto& E : MeshesMerge) E = 1;

            //
            // fill the list for all the meshes that can not be merged
            //
            iMergedMesh = 0;
            for (auto E : m_Descriptor.m_MeshList)
            {
                auto index = m_RawGeom.findMesh(E.m_OriginalName);
                if (index == -1)
                {
                    LogMessage(xresource_pipeline::msg_type::WARNING, std::format("Mesh {} is not longer associated it will be marked for the merged", E.m_OriginalName));
                    continue;
                }

                MeshesMerge[index] = E.m_bMerge?1:0;

                if (MeshesMerge[index]==0) iMergedMesh++;   
            }

            //
            // Create the remap table
            //

            // Find the first mesh that is going to be deleted
            int iLastEmpty = 0;
            for(auto& E : MeshesMerge)
            {
                const int index = static_cast<int>(&E - MeshesMerge.data());
                if (E == 1) RemapMeshes[index] = iMergedMesh;
                else
                {
                    RemapMeshes[index] = iLastEmpty;
                    iLastEmpty++;
                }
            }

            //
            // Update the faces with the right index
            //
            for (auto& Facet : m_RawGeom.m_Facet)
            {
                Facet.m_iMesh = RemapMeshes[Facet.m_iMesh];
            }

            //
            // Remove all meshes that have been merged
            //
            for (auto& E : m_RawGeom.m_Mesh)
            {
                const int index = static_cast<int>(&E - m_RawGeom.m_Mesh.data());

                // If it is a deleted mesh we do not need to worry about it
                if (RemapMeshes[index] == iMergedMesh)
                    continue;

                // Check if it's actually going to move or not...
                if ( index != RemapMeshes[index] )
                {
                    m_RawGeom.m_Mesh[RemapMeshes[index]] = std::move(m_RawGeom.m_Mesh[index]);
                }
            }

            //
            // The new merge mesh
            //
            m_RawGeom.m_Mesh[iMergedMesh].m_Name    = "Merged Mesh";
            m_RawGeom.m_Mesh[iMergedMesh].m_nBones  = 0;
            m_RawGeom.m_Mesh.resize(iMergedMesh+1);
        }

        //--------------------------------------------------------------------------------------

        xerr Compile()
        {
            try
            {
                if (m_Descriptor.m_bMergeMeshes) 
                {
                    displayProgressBar("Merging Meshes", 1);
                    MergeMeshes();
                    displayProgressBar("Merging Meshes", 0);
                }

                displayProgressBar("Cleaning up Geom", 1);
                m_RawGeom.CleanMesh();
                m_RawGeom.SortFacetsByMeshMaterialBone();
                displayProgressBar("Cleaning up Geom", 1);

                //
                // Convert to mesh
                //
                m_FinalGeom.Initialize();

                displayProgressBar("Generating LODs", 1);
                ConvertToCompilerMesh();
                GenenateLODs();
                displayProgressBar("Generating LODs", 0);

                displayProgressBar("Optimizing", 0);
                optimizeFacesAndVerts();
                displayProgressBar("Optimizing", 1);

                //
                // Generate final mesh
                //
                displayProgressBar("Generating Final Mesh", 0);
                // mm accuracy
                ConvertToGeom(0.001f);
                
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
            m_Details.m_Meshes.resize(m_RawGeom.m_Mesh.size());
            for ( auto& M : m_RawGeom.m_Mesh )
            {
                const auto  index   = static_cast<int>(&M - m_RawGeom.m_Mesh.data());
                auto&       DM      = m_Details.m_Meshes[index];

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
                m_Details.m_Meshes[F.m_iMesh].m_NumFaces++;

                for ( int i=0; i<F.m_nVertices; ++i)
                {
                    m_Details.m_Meshes[F.m_iMesh].m_NumColors   = std::max(m_Details.m_Meshes[F.m_iMesh].m_NumColors,   m_RawGeom.m_Vertex[F.m_iVertex[0]].m_nColors );
                    m_Details.m_Meshes[F.m_iMesh].m_NumUVs      = std::max(m_Details.m_Meshes[F.m_iMesh].m_NumUVs,      m_RawGeom.m_Vertex[F.m_iVertex[0]].m_nUVs    );
                    m_Details.m_Meshes[F.m_iMesh].m_NumNormals  = std::max(m_Details.m_Meshes[F.m_iMesh].m_NumNormals,  m_RawGeom.m_Vertex[F.m_iVertex[0]].m_nNormals);
                    m_Details.m_Meshes[F.m_iMesh].m_NumTangents = std::max(m_Details.m_Meshes[F.m_iMesh].m_NumTangents, m_RawGeom.m_Vertex[F.m_iVertex[0]].m_nTangents);
                }

                MeshMatUsage[F.m_iMesh][F.m_iMaterialInstance]++;
            }

            // Set the material instance counts...
            for ( auto& M : m_Details.m_Meshes )
            {
                const auto  index = static_cast<int>(&M - m_Details.m_Meshes.data());
                M.m_NumMaterials = 0;
                for ( auto& I : MeshMatUsage[index] ) M.m_NumMaterials += I?1:0;
            }

            // Find total faces
            m_Details.m_NumFaces        = 0;
            m_Details.m_NumMaterials    = static_cast<int>(m_RawGeom.m_MaterialInstance.size());
            for (auto& M : m_Details.m_Meshes)
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
            if (0)
            {
                xproperty::settings::context    Context{};
                auto                            DescriptorFileName = std::format(L"{}/{}/Descriptor.txt", m_ProjectPaths.m_Project, m_InputSrcDescriptorPath);

                if ( auto Err = m_Descriptor.Serialize(true, DescriptorFileName, Context); Err)
                    return Err;
            }

            //
            // Do a quick validation of the descriptor
            //
            if (0)
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


            m_Descriptor.m_ImportAsset = std::wstring(L"Assets\\materialpreview-frozen.fbx");
            m_Descriptor.m_ImportAsset = std::wstring(L"Assets\\PuppyDog\\source\\Puppy Robot2.fbx");
            m_Descriptor.m_bMergeMeshes = true;

            //
            // Load the source data
            //
            displayProgressBar("Loading Mesh", 0);
            if ( auto Err = LoadRaw(std::format(L"{}/{}", m_ProjectPaths.m_Project, m_Descriptor.m_ImportAsset)); Err )
                return Err;
            displayProgressBar("Loading Mesh", 1);

            //
            // Fill the detail structure
            //
            ComputeDetailStructure();

            //
            // OK Time to compile
            //
            Compile();

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
                , m_OptimizationType == optimization_type::O0     ? xserializer::compression_level::FAST : xserializer::compression_level::HIGH
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

        xgeom_static::details           m_Details;
        xgeom_static::descriptor        m_Descriptor;

        xgeom_static::geom              m_FinalGeom;
        std::vector<mesh>               m_CompilerMesh;
        xraw3d::geom                    m_RawGeom;
    };

    //------------------------------------------------------------------------------------

    std::unique_ptr<instance> instance::Create(void)
    {
        return std::make_unique<implementation>();
    }
}


