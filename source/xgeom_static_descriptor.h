#ifndef XGEOM_STATIC_DESCRIPTOR_H
#define XGEOM_STATIC_DESCRIPTOR_H
#pragma once

#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"

namespace xgeom_static
{
    // While this should be just a type... it also happens to be an instance... the instance of the texture_plugin
    // So while generating the type guid we must treat it as an instance.
    inline static constexpr auto resource_type_guid_v = xresource::type_guid(xresource::guid_generator::Instance64FromString("GeomStatic"));

    static constexpr wchar_t mesh_filter_v[]        = L"Mesh\0 *.fbx; *.obj\0Any Thing\0 *.*\0";

    struct lod
    {
        float               m_LODReduction  = 0.7f;
        float               m_ScreenArea    = 1;            // in pixels
        XPROPERTY_DEF
        ("lod", lod
        , obj_member<"LODReduction",    &lod::m_LODReduction >
        , obj_member<"ScreenArea",      &lod::m_ScreenArea >
        )
    };
    XPROPERTY_REG(lod)

    struct mesh
    {
        using mati_list = std::vector<xrsc::material_instance_ref>;
        std::string         m_OriginalName          = {};
        bool                m_bMerge                = true;
        std::uint32_t       m_MeshGUID              = {};
        int                 m_NumberOfLODS          = 0;
        std::vector<lod>    m_LODs                  = {};
        mati_list           m_DefaultMaterialsI     = {} ;

        XPROPERTY_DEF
        ("mesh", mesh
        , obj_member<"OriginalName",        &mesh::m_OriginalName, member_flags< flags::SHOW_READONLY> >
        , obj_member<"Merge",                &mesh::m_bMerge >
        , obj_member<"MeshGUID",            &mesh::m_MeshGUID >
        , obj_member<"NumberOfLODS",        &mesh::m_NumberOfLODS >
        , obj_member<"LODs",                &mesh::m_LODs >
        , obj_member<"DefaultMaterials",    &mesh::m_DefaultMaterialsI >
        )
    };
    XPROPERTY_REG(mesh)

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

    struct data
    {
        int                 m_nUVs          = 1;
        int                 m_nColors       = 0;

        XPROPERTY_DEF
        ( "data", pre_transform
        , obj_member<"NumUVs",           &data::m_nUVs >
        , obj_member<"NumColors",        &data::m_nColors >
        )
    };
    XPROPERTY_REG(data)

    struct descriptor : xresource_pipeline::descriptor::base
    {
        using parent = xresource_pipeline::descriptor::base;

        void SetupFromSource(std::string_view FileName) override
        {
        }

        void Validate(std::vector<std::string>& Errors) const noexcept override
        {
        }

        int findMesh(std::string_view Name)
        {
            for ( auto&E : m_MeshList)
                if ( E.m_OriginalName == Name ) return static_cast<int>(&E - m_MeshList.data());
            return -1;
        }

        std::wstring        m_ImportAsset       = {};
        pre_transform       m_PreTranslation    = {};
        bool                m_bMergeMeshes      = true;
        std::uint64_t       m_MergedMeshGUID    = {};
        data                m_Data              = {};
        bool                m_bHideMergedMeshes = false;
        std::vector<mesh>   m_MeshList          = {};

        XPROPERTY_VDEF
        ("GeomStatic", descriptor
        , obj_member<"ImportAsset",         &descriptor::m_ImportAsset >
        , obj_member<"PreTranslation",      &descriptor::m_PreTranslation >
        , obj_member<"Data",                &descriptor::m_Data >
        , obj_member<"bMergeMeshes",        &descriptor::m_bMergeMeshes >
        , obj_member<"MergedMeshGUID",      &descriptor::m_MergedMeshGUID, member_flags< flags::SHOW_READONLY>  >
        , obj_member<"bHideMergedMeshes",   &descriptor::m_bHideMergedMeshes >
        , obj_member<"MeshList",            &descriptor::m_MeshList >
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
}
#endif