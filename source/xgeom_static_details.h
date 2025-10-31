#ifndef XGEOM_STATIC_DETAILS_H
#define XGEOM_STATIC_DETAILS_H
#pragma once

namespace xgeom_static
{
    struct details
    {
        struct mesh
        {
            std::string         m_Name          = {};
            int                 m_NumMaterials  = 0;
            int                 m_NumFaces      = 0;
            int                 m_NumUVs        = 0;
            int                 m_NumColors     = 0;
            int                 m_NumNormals    = 0;
            int                 m_NumTangents   = 0;

            XPROPERTY_DEF
            ( "mesh", mesh
            , obj_member<"Name",            &mesh::m_Name >
            , obj_member<"NumMaterials",    &mesh::m_NumMaterials >
            , obj_member<"NumFaces",        &mesh::m_NumFaces >
            , obj_member<"NumUVs",          &mesh::m_NumUVs >
            , obj_member<"NumColors",       &mesh::m_NumColors >
            )
        };

        std::vector<mesh>   m_Meshes;
        int                 m_NumFaces;
        int                 m_NumMaterials;

        XPROPERTY_DEF
        ("details", details
        , obj_member<"Name",            &details::m_Meshes >
        , obj_member<"NumFaces",        &details::m_NumFaces >
        , obj_member<"NumMaterials",    &details::m_NumMaterials >
        )
    };
    XPROPERTY_REG(details)
    XPROPERTY_REG2(mesh_, details::mesh)
}

#endif
