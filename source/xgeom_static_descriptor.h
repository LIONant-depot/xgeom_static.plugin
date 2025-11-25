#ifndef XGEOM_STATIC_DESCRIPTOR_H
#define XGEOM_STATIC_DESCRIPTOR_H
#pragma once

#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"
#include "xgeom_static_details.h"

namespace xgeom_static
{
    // While this should be just a type... it also happens to be an instance... the instance of the texture_plugin
    // So while generating the type guid we must treat it as an instance.
    inline static constexpr auto    resource_type_guid_v    = xresource::type_guid(xresource::guid_generator::Instance64FromString("GeomStatic"));
    static constexpr wchar_t        mesh_filter_v[]         = L"Mesh\0 *.fbx; *.obj\0Any Thing\0 *.*\0";

    struct lod
    {
        float               m_LODReduction  = 0.7f;
        float               m_ScreenArea    = 1;            // in pixels
        XPROPERTY_DEF
        ( "lod", lod
        , obj_member<"LODReduction",    &lod::m_LODReduction >
        , obj_member<"ScreenArea",      &lod::m_ScreenArea >
        )
    };
    XPROPERTY_REG(lod)

    struct pre_transform
    {
        xmath::fvec3        m_Scale         = xmath::fvec3::fromOne();
        xmath::fvec3        m_Rotation      = xmath::fvec3::fromZero();
        xmath::fvec3        m_Translation   = xmath::fvec3::fromZero();

        XPROPERTY_DEF
        ( "preTransform", pre_transform
        , obj_member<"Scale",           &pre_transform::m_Scale >
        , obj_member<"Rotation",        &pre_transform::m_Rotation >
        , obj_member<"Translation",     &pre_transform::m_Translation >
        )
    };
    XPROPERTY_REG(pre_transform)

/*
    struct data
    {
        int                 m_nUVs          = 1;
        int                 m_nColors       = 0;

        XPROPERTY_DEF
        ( "data", data
        , obj_member<"NumUVs",           &data::m_nUVs >
        , obj_member<"NumColors",        &data::m_nColors >
        )
    };
    XPROPERTY_REG(data)
*/

    struct mesh_details
    {
        std::uint32_t               m_GUID                  = {};
        std::vector<lod>            m_LODs                  = {};

        XPROPERTY_DEF
        ( "mesh_details", mesh_details
        , obj_member<"GUID",                &mesh_details::m_GUID >
        , obj_member<"LODs",                &mesh_details::m_LODs >
        )
    };
    XPROPERTY_REG(mesh_details)

    using node_path = std::vector<std::string>;

    struct ungroup_mesh
    {
        node_path                   m_NodePath;         // The list of nodes that took to arrive to the mesh
        std::string                 m_MeshName;         // The actual mesh name
        mesh_details                m_MeshDetails;

        XPROPERTY_DEF
        ( "ungroup_mesh", ungroup_mesh
        , obj_member<"Node Path",       &ungroup_mesh::m_NodePath, member_flags< flags::SHOW_READONLY> >
        , obj_member<"Mesh Name",       &ungroup_mesh::m_MeshName, member_flags< flags::SHOW_READONLY> >
        , obj_member<"Mesh Details",    &ungroup_mesh::m_MeshDetails, member_ui_open<true> >
        )
    };
    XPROPERTY_REG(ungroup_mesh)

    struct merge_group
    {
        std::string                 m_Name;
        std::vector<node_path>      m_NodePathList;
        mesh_details                m_MeshDetails;

        XPROPERTY_DEF
        ( "merge_group", merge_group
        , obj_member<"Name",           &merge_group::m_Name >
        , obj_member<"Node Path List", &merge_group::m_NodePathList, member_flags< flags::SHOW_READONLY> >
        , obj_member<"Mesh Details",   &merge_group::m_MeshDetails >
        )
    };
    XPROPERTY_REG(merge_group)

    struct delete_entry
    {
        node_path                   m_NodePath;
        std::string                 m_MeshName;

        XPROPERTY_DEF
        ( "delete_entry", delete_entry
        , obj_member<"Node Path",       &delete_entry::m_NodePath, member_flags< flags::SHOW_READONLY> >
        , obj_member<"Mesh Name",       &delete_entry::m_MeshName, member_flags< flags::SHOW_READONLY> >
        )
    };
    XPROPERTY_REG(delete_entry)

    struct material_details
    {
        std::string                 m_Name;
        int                         m_RefCount=0;
        XPROPERTY_DEF
        ( "material_details", material_details
        , obj_member<"Name",        &material_details::m_Name, member_flags< flags::SHOW_READONLY> >
        , obj_member<"RefCount",    &material_details::m_RefCount, member_flags< flags::SHOW_READONLY> >
        )
    };
    XPROPERTY_REG(material_details)

    //------------------------------------------------------------------------------------------------
    // HELPFUL FUNCTIONS
    //------------------------------------------------------------------------------------------------

    inline
    bool CompareNodePaths(std::span<const std::string> A, std::span<const std::string> B) noexcept
    {
        if (A.size() != B.size()) return false;
        if (A.empty() && B.empty()) return false;

        for (int i = 0; i < A.size(); ++i)
        {
            if (A[i] != B[i]) return false;
        }

        return true;
    }

    inline
    std::string NodePathToString(const node_path& NodePath) noexcept
    {
        std::string Path = std::format("{}", NodePath[0]);
        for (auto& s : std::span(NodePath).subspan(1)) Path = std::format("{}/{}", Path, s);
        return Path;
    }

    //------------------------------------------------------------------------------------------------
    // DESCRIPTOR
    //------------------------------------------------------------------------------------------------
    struct descriptor : xresource_pipeline::descriptor::base
    {
        using parent = xresource_pipeline::descriptor::base;

        void SetupFromSource(std::string_view FileName) override
        {
        }

        void Validate(std::vector<std::string>& Errors) const noexcept override
        {
        }

        int findMaterial( std::string_view Name )
        {
            for (auto& E : m_MaterialDetailsList)
                if (E.m_Name == Name) return static_cast<int>(&E - m_MaterialDetailsList.data());
            return -1;
        }

        bool isNodeInGroup(const node_path& NodePath) const
        {
            for (int i = 0; i < m_MergeGroupList.size(); ++i)
            {
                for ( auto& n : m_MergeGroupList[i].m_NodePathList )
                {
                    if (NodePath.size() < n.size())
                        continue;

                    if (CompareNodePaths(n, std::span(NodePath).subspan(0, n.size())))
                        return true;
                }
            }

            return false;
        }

        bool isNodeInDeleteList(const node_path& NodePath) const
        {
            for (auto& e : m_DeleteEntryList)
            {
                if (NodePath.size() < e.m_NodePath.size())
                    continue;

                // We are a node deletion
                if (e.m_MeshName.empty())
                {
                    if (CompareNodePaths(e.m_NodePath, std::span(NodePath).subspan(0, e.m_NodePath.size())))
                        return true;
                }
            }

            return false;
        }

        bool isMeshInDeleteList(const node_path& NodePath, std::string_view MeshName) const
        {
            for ( auto& e : m_DeleteEntryList)
            {
                if (NodePath.size() < e.m_NodePath.size())
                    continue;

                // We are a node deletion
                if (e.m_MeshName.empty())
                {
                    if (CompareNodePaths(e.m_NodePath, std::span(NodePath).subspan(0, e.m_NodePath.size())))
                        return true;
                }
                else
                {
                    if (e.m_NodePath.size() != NodePath.size() )
                        continue;

                    if (CompareNodePaths(e.m_NodePath, std::span(NodePath).subspan(0, e.m_NodePath.size())))
                    {
                        if (e.m_MeshName == MeshName )
                            return true;
                    }
                }
            }

            return false;
        }

        std::pair<merge_group*, int> findMergeGroupFromNode( const node_path& NodePath )
        {
            for ( auto& mg : m_MergeGroupList )
            {
                for ( auto& n : mg.m_NodePathList)
                {
                    if ( CompareNodePaths(NodePath, n) )
                    {
                        return { &mg, static_cast<int>(&n - mg.m_NodePathList.data())};
                    }
                }
            }

            return {};
        }

        int findUngroupMesh( const node_path& NodePath, std::string_view MeshName ) const
        {
            for ( auto& m : m_UngroupMeshList )
            {
                if ( CompareNodePaths( NodePath, m.m_NodePath) && MeshName == m.m_MeshName )
                    return static_cast<int>(&m - m_UngroupMeshList.data());
            }

            return -1;
        }

        void AddNodeInGroupList(merge_group& Group, const node_path& NodePath)
        {
            //
            // Make sure to remove it from any other group first
            //
            for ( auto& g : m_MergeGroupList )
            {
                for ( int i=0; i< g.m_NodePathList.size(); ++i )
                {
                    auto& n = g.m_NodePathList[i];

                    if ( CompareNodePaths(NodePath,n) )
                    {
                        g.m_NodePathList.erase(g.m_NodePathList.begin()+i);
                        --i;
                    }
                }
            }

            //
            // Make sure that any meshes inside the ungroup section are removed
            //
            RemoveAllNodeMeshesFromUngroupList(NodePath);

            //
            // Add entry into new group
            //
            Group.m_NodePathList.push_back({ NodePath });;
        }

        void RemoveNodeFromGroup(merge_group& Group, int iNode, const details& Details )
        {
            //
            // Add meshes into unorder group
            //
            auto DetailPair = Details.findNode( Group.m_NodePathList[iNode] );
            assert(DetailPair.first );

            node_path Path = Group.m_NodePathList[iNode];
            std::function<void(const details::node&)> Recursize = [&](const details::node& Node)
            {
                // If this node is mark as deleted then we should skip everything...
                if (isNodeInDeleteList(Path))
                    return;

                // Add all the meshes
                for( auto idx : Node.m_MeshList )
                {
                    m_UngroupMeshList.push_back( { Path, Details.m_MeshList[idx].m_Name } );
                }

                // Include children
                for ( auto& c : Node.m_Children )
                {
                    Path.push_back(c.m_Name);
                    Recursize(c);
                    Path.pop_back();
                }
            };
            Recursize(*DetailPair.first);

            //
            // Remove the Node from the actual group
            //
            Group.m_NodePathList.erase(Group.m_NodePathList.begin()+ iNode);
        }

        void RemoveAllNodeMeshesFromUngroupList(const node_path& NodePath)
        {
            for (int i = 0; i < m_UngroupMeshList.size(); ++i)
            {
                if (m_UngroupMeshList[i].m_NodePath.size() < NodePath.size())
                    continue;

                if (CompareNodePaths(std::span(m_UngroupMeshList[i].m_NodePath).subspan(0, NodePath.size()), NodePath) == false)
                    continue;


                // This means that the node is the parent of this mesh so remove it
                m_UngroupMeshList.erase(m_UngroupMeshList.begin() + i);
                --i;
            }
        }

        void AddAllNodeMeshesToUngroupList(const node_path& NodePath, const details& Details)
        {
            //
            // Add meshes into unorder group
            //
            auto Pair = Details.findNode(NodePath);
            assert(Pair.first);

            node_path Path = NodePath;
            std::function<void(const details::node&)> Recursize = [&](const details::node& Node)
                {
                    // Add all the meshes
                    for (auto idx : Node.m_MeshList)
                    {
                        m_UngroupMeshList.push_back({ Path, Details.m_MeshList[idx].m_Name });
                    }

                    // Include children
                    for (auto& c : Node.m_Children)
                    {
                        Path.push_back(c.m_Name);
                        Recursize(c);
                        Path.pop_back();
                    }
                };
            Recursize(*Pair.first);
        }

        void AddNodeInDeleteList(const node_path& NodePath, const details& Details)
        {
            auto Pair = findMergeGroupFromNode(NodePath);

            // If it is in a group remove it!
            if (Pair.first) RemoveNodeFromGroup(*Pair.first, Pair.second, Details);
            RemoveAllNodeMeshesFromUngroupList(NodePath);

            // Make sure it does not exists in the DeleteList
            if (isNodeInDeleteList(NodePath))
                return;

            // OK simply add it
            m_DeleteEntryList.push_back({ NodePath });

            //
            // Recompute the material references
            //
            RecomputeMaterialReferences(Details);
        }

        void RemoveNodeFromDeleteList(const node_path& NodePath, const details& Details)
        {
            //
            // Remove the Node from the undeleted list
            //
            for (auto& t : m_DeleteEntryList)
            {
                if (t.m_MeshName.empty() && CompareNodePaths(t.m_NodePath, NodePath) )
                {
                    m_DeleteEntryList.erase(m_DeleteEntryList.begin() + static_cast<int>( &t - m_DeleteEntryList.data()));
                }
            }

            //
            // Re-add all the Node meshes to the ungroup-list
            //
            if ( isNodeInGroup(NodePath) == false)
            {
                AddAllNodeMeshesToUngroupList(NodePath, Details);
            }

            //
            // Recompute the material references
            //
            RecomputeMaterialReferences(Details);
        }

        void RecomputeMaterialReferences(const details& Details)
        {
            // clear out all the references
            for ( auto& m : m_MaterialDetailsList) m.m_RefCount = 0;

            // Start adding references...
            node_path Path;
            Path.push_back(Details.m_RootNode.m_Name);
            std::function<void(const details::node&)> Recursize = [&](const details::node& Node)
                {
                    // If this node is mark as deleted then we should skip everything...
                    if (isNodeInDeleteList(Path))
                        return;

                    // Add all the meshes
                    for (auto idx : Node.m_MeshList)
                    {
                        for ( auto MatIdx : Details.m_MeshList[idx].m_MaterialList )
                        {
                            auto Index = findMaterial(Details.m_MaterialList[MatIdx]);
                            m_MaterialDetailsList[Index].m_RefCount++;
                        }
                    }

                    // Include children
                    for (auto& c : Node.m_Children)
                    {
                        Path.push_back(c.m_Name);
                        Recursize(c);
                        Path.pop_back();
                    }
                };
            Recursize(Details.m_RootNode);
        }


        inline std::vector<std::string> MergeWithDetails( xgeom_static::details& Details );

        std::wstring                                m_ImportAsset                   = {};
        pre_transform                               m_PreTranslation                = {};
        bool                                        m_bMergeAllMeshes               = true;
        mesh_details                                m_AllMeshesDetails              = {};
        std::vector<material_details>               m_MaterialDetailsList           = {};
        std::vector<xrsc::material_instance_ref>    m_MaterialInstRefList           = {};
        std::vector<ungroup_mesh>                   m_UngroupMeshList               = {};
        std::vector<merge_group>                    m_MergeGroupList                = {};
        std::vector<delete_entry>                   m_DeleteEntryList               = {};

        XPROPERTY_VDEF
        ( "GeomStatic", descriptor
        , obj_member<"ImportAsset",         &descriptor::m_ImportAsset, member_ui<std::wstring>::file_dialog<mesh_filter_v, true, 1> >
        , obj_member<"PreTranslation",      &descriptor::m_PreTranslation >
        , obj_member<"bMergeAllMeshes",     &descriptor::m_bMergeAllMeshes >
        , obj_member<"AllMeshesDetails",    &descriptor::m_AllMeshesDetails, member_ui_open<true>, member_dynamic_flags<+[](const descriptor& O)
            {
                xproperty::flags::type Flags = {};
                Flags.m_bDontShow = !O.m_bMergeAllMeshes;
                return Flags;
            }
            >>
        , obj_member<"Merge Group List",    &descriptor::m_MergeGroupList, member_dynamic_flags < +[](const descriptor& O)
            {
                xproperty::flags::type Flags = {};
                Flags.m_bDontShow = O.m_bMergeAllMeshes;
                return Flags;
            }
            >>
        , obj_member<"Ungroup Mesh List",   &descriptor::m_UngroupMeshList, member_dynamic_flags<+[](const descriptor& O)
            {
                xproperty::flags::type Flags = {};
                Flags.m_bDontShow = O.m_bMergeAllMeshes;
                return Flags;
            }
            >>
        , obj_member<"Deleted List",          &descriptor::m_DeleteEntryList >
        , obj_member<"MaterialDetailsList",   &descriptor::m_MaterialDetailsList, member_flags<flags::DONT_SHOW>>
        , obj_member<"MaterialInstance",      &descriptor::m_MaterialInstRefList, member_ui_open<true> >
        )
    };
    XPROPERTY_VREG(descriptor)

    //--------------------------------------------------------------------------------------

    struct factory final : xresource_pipeline::factory_base
    {
        using xresource_pipeline::factory_base::factory_base;

        std::unique_ptr<xresource_pipeline::descriptor::base> CreateDescriptor(void) const noexcept override
        {
            return std::make_unique<descriptor>();
        };

        xresource::type_guid ResourceTypeGUID(void) const noexcept override
        {
            return resource_type_guid_v;
        }

        const char* ResourceTypeName(void) const noexcept override
        {
            return "GeomStatic";
        }

        const xproperty::type::object& ResourceXPropertyObject(void) const noexcept override
        {
            return *xproperty::getObjectByType<descriptor>();
        }
    };

    inline static factory g_Factory{};

    //--------------------------------------------------------------------------------------
    inline
    std::vector<std::string> descriptor::MergeWithDetails(xgeom_static::details& Details )
    {
        std::vector<std::string> Messages;

        //------------------------------------------------------------------------------------------
        // Try to remove entries from our list base on the new state of details...
        //------------------------------------------------------------------------------------------

        //
        // Merge with Merge-Group-List
        //
        for( auto& g : m_MergeGroupList)
        {
            for( int i=0; i< g.m_NodePathList.size(); ++i )
            {
                if ( auto Pair = Details.findNode(g.m_NodePathList[i]); Pair.first)
                {
                    // Great we still have the node...
                }
                else
                {
                    Messages.push_back(std::format("WARNING: Failed to find a node [{}] We will have to remove it from the Merge group list", NodePathToString(g.m_NodePathList[i])));

                    // We no longer have this node!
                    g.m_NodePathList.erase(g.m_NodePathList.begin()+i);
                    --i;
                }
            }
        }

        //
        // Merge the Ungroup mesh list
        //
        for( int i=0; i< m_UngroupMeshList.size(); ++i )
        {
            if (auto Pair = Details.findMesh(m_UngroupMeshList[i].m_NodePath, m_UngroupMeshList[i].m_MeshName); Pair.first)
            {
                // Great we still have our mesh!
            }
            else
            {

                // OK can we find if the mesh still exist at all...
                if ( int Idx = Details.findMesh(m_UngroupMeshList[i].m_MeshName); Idx == -1)
                {
                    Messages.push_back(std::format("WARNING: Failed to find a mesh [{}] who used to be in this node [{}]. The mesh have been completly removed. We will remove from the ungrouped mesh list."
                                                        , m_UngroupMeshList[i].m_MeshName, NodePathToString(m_UngroupMeshList[i].m_NodePath)));
                }
                else
                {
                    Messages.push_back(std::format("WARNING: Failed to find a mesh [{}] who used to be in this node [{}]. We will remove from the ungrouped mesh list."
                                                        , m_UngroupMeshList[i].m_MeshName, NodePathToString(m_UngroupMeshList[i].m_NodePath)));
                }

                // Remove it from our list
                m_UngroupMeshList.erase(m_UngroupMeshList.begin()+i);
                --i;
            }
        }

        //
        // Merge Meshes to be deleted...
        //
        for ( int i=0; i<m_DeleteEntryList.size(); ++i)
        {
            // Check the case where we are deleting nodes
            if (not m_DeleteEntryList[i].m_NodePath.empty() && m_DeleteEntryList[i].m_MeshName.empty() )
            {
                if (auto Pair = Details.findNode(m_DeleteEntryList[i].m_NodePath); Pair.first)
                {
                    // Great we still have the node
                }
                else
                {
                    Messages.push_back(std::format("WARNING: We not longer found node [{}] we will remove it from the Delte Entry List."
                                                    , NodePathToString(m_DeleteEntryList[i].m_NodePath)));

                    // We do not have the node so we will remove it from our list
                    m_DeleteEntryList.erase(m_DeleteEntryList.begin()+i);
                    --i;
                }
            }
            // Check the case where we are deleting meshes
            else if (not m_DeleteEntryList[i].m_NodePath.empty() && not m_DeleteEntryList[i].m_MeshName.empty())
            {
                if (auto Pair = Details.findMesh(m_DeleteEntryList[i].m_NodePath, m_DeleteEntryList[i].m_MeshName); Pair.first)
                {
                    // Great we still have the mesh
                }
                else
                {
                    Messages.push_back(std::format("WARNING: Failed to find a mesh [{}] who used to be in this node [{}]. We will remove from the delete entry list."
                                                    , m_DeleteEntryList[i].m_MeshName, NodePathToString(m_DeleteEntryList[i].m_NodePath)));

                    // We do not have the node so we will remove it from our list
                    m_DeleteEntryList.erase(m_DeleteEntryList.begin() + i);
                    --i;
                }
            }
            // Check the case where we are deleteting all the mesh references
            else if (m_DeleteEntryList[i].m_NodePath.empty() && not m_DeleteEntryList[i].m_MeshName.empty())
            {
                if (auto idx = Details.findMesh(m_DeleteEntryList[i].m_MeshName); idx != -1 )
                {
                    // Great we still removing all these meshes
                }
                else
                {
                    Messages.push_back(std::format("WARNING: Failed to find a mesh [{}] so We will remove from the delete entry list.", m_DeleteEntryList[i].m_MeshName));

                    // We do not have the node so we will remove it from our list
                    m_DeleteEntryList.erase(m_DeleteEntryList.begin() + i);
                    --i;
                }
            }
            else
            {
                // We should not have such case...
                m_DeleteEntryList.erase(m_DeleteEntryList.begin() + i);
                --i;
            }
        }

        //------------------------------------------------------------------------------------------
        // Try to add any new mesh from details that is currently not in our list of ungroup meshes
        //------------------------------------------------------------------------------------------
        {
            std::function<void(const details::node&, const node_path&)> Recurse = [&](const details::node& Node, const node_path& NodePath)
            {
                node_path NewPath = NodePath;
                NewPath.push_back(Node.m_Name);

                // This path is already part of the group so just skip everything
                if ( auto Pair = findMergeGroupFromNode(NewPath); Pair.first != nullptr )
                    return;

                // Add meshes from this node
                for (auto idx : Node.m_MeshList)
                {
                    if (findUngroupMesh(NewPath, Details.m_MeshList[idx].m_Name) == -1)
                    {
                        // OK we do not have this mesh...
                        // Check if we may have mark it for deletion if not it must be a new mesh
                        if (isMeshInDeleteList(NewPath, Details.m_MeshList[idx].m_Name) == false)
                            m_UngroupMeshList.push_back({ NewPath, Details.m_MeshList[idx].m_Name });
                    }
                    else
                    {
                        // We have this mesh alreay... so nothing to do...
                    }
                }

                // OK let the children add meshes if they need
                for (auto& c : Node.m_Children)
                {
                    Recurse( c, NewPath);
                }
            };

            //
            // Add all the ungroup meshes into our list
            //
            node_path NodePath;
            Recurse(Details.m_RootNode, NodePath );
        }

        //---------------------------------------------------------------------------------------------
        // Update the materials
        //---------------------------------------------------------------------------------------------

        // First remove any materials no longer in used
        for (int i = 0; i < m_MaterialDetailsList.size(); ++i)
        {
            if (Details.findMaterial(m_MaterialDetailsList[i].m_Name) == -1)
            {
                m_MaterialDetailsList.erase(m_MaterialDetailsList.begin() + i);
                m_MaterialInstRefList.erase(m_MaterialInstRefList.begin() + i);
                --i;
            }
        }

        // Add all new Materials if we have to...
        for (auto& E : Details.m_MaterialList)
        {
            if (auto Index = findMaterial(E); Index == -1)
            {
                m_MaterialDetailsList.emplace_back(E);
                m_MaterialInstRefList.emplace_back();
            }
        }

        // Recompute material references
        RecomputeMaterialReferences(Details);


        return Messages;
    }


}




#endif