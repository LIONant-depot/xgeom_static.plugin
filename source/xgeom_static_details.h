#ifndef XGEOM_STATIC_DETAILS_H
#define XGEOM_STATIC_DETAILS_H
#pragma once

namespace xgeom_static
{
    struct details
    {
        struct mesh
        {
            std::string                 m_Name          = {};
            std::vector<std::string>    m_MaterialList  = {};
            int                         m_NumFaces      = 0;
            int                         m_NumUVs        = 0;
            int                         m_NumColors     = 0;
            int                         m_NumNormals    = 0;
            int                         m_NumTangents   = 0;

            XPROPERTY_DEF
            ( "mesh", mesh
            , obj_member<"Name",            &mesh::m_Name >
            , obj_member<"NumMaterials",    &mesh::m_MaterialList >
            , obj_member<"NumFaces",        &mesh::m_NumFaces >
            , obj_member<"NumUVs",          &mesh::m_NumUVs >
            , obj_member<"NumColors",       &mesh::m_NumColors >
            )
        };


        int findMesh( std::string_view Name ) const noexcept
        {
            for ( auto& E : m_Meshes)
                if ( E.m_Name == Name ) return static_cast<int>(&E - m_Meshes.data());
            return -1;
        }

        int findMaterial(std::string_view Name) const noexcept
        {
            for (auto& E : m_MaterialList)
                if (E == Name) return static_cast<int>(&E - m_MaterialList.data());
            return -1;
        }

        std::vector<std::string>    m_MaterialList;
        std::vector<mesh>           m_Meshes;
        int                         m_NumFaces;

        XPROPERTY_DEF
        ( "details", details
        , obj_member<"Meshes",              &details::m_Meshes, member_flags<flags::SHOW_READONLY> >
        , obj_member<"NumFaces",            &details::m_NumFaces, member_flags<flags::SHOW_READONLY> >
        , obj_member<"MaterialList",        &details::m_MaterialList, member_flags<flags::SHOW_READONLY> >
        )
    };
    XPROPERTY_REG(details)
    XPROPERTY_REG2(mesh_, details::mesh)
}

#endif
