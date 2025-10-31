#include "xgeom_static_xgpu_runtime.h"
#include "xgeom_static_xgpu_rsc_loader.h"

#include "dependencies/xresource_guid/source/bridges/xresource_xproperty_bridge.h"

//
// We will register the loader, the properties, 
//
inline static auto s_GeomRegistrations = xresource::common_registrations<xrsc::geom_static_type_guid_v>{};

//------------------------------------------------------------------

xresource::loader< xrsc::geom_type_guid_v >::data_type* xresource::loader< xrsc::geom_type_guid_v >::Load(xresource::mgr& Mgr, const full_guid& GUID)
{
    auto&                   UserData    = Mgr.getUserData<resource_mgr_user_data>();
    std::wstring            Path        = Mgr.getResourcePath(GUID, type_name_v);
    xgeom_static::geom*     pGeom       = nullptr;

    // Load the xgeom_static
    xserializer::stream Stream;
    if (auto Err = Stream.Load(Path, pGeom); Err)
    {
        assert(false);
    }

    //
    // These registration should be factor-out
    // 
    xgpu::vertex_descriptor VertexDescriptor;
    auto Attributes = std::array
    {
        xgpu::vertex_descriptor::attribute
        {
            .m_Offset = 0,
            .m_Format = xgpu::vertex_descriptor::format::SHORT_3D  // Adjust for SNORM if supported
        },  
        xgpu::vertex_descriptor::attribute
        {
            .m_Offset = sizeof(short)*3,
            .m_Format = xgpu::vertex_descriptor::format::SHORT_1D   // Extra as int
        }  
    };
    UserData.m_Device.Create(VertexDescriptor, { .m_VertexSize = sizeof(xgeom_static::geom::vertex), .m_Attributes = Attributes });

    // Extras descriptor (UV UNORM16_2, oct SNORM16_2 x2)
    xgpu::vertex_descriptor ExtrasDescriptor;
    auto ExtrasAttributes = std::array
    {
        xgpu::vertex_descriptor::attribute
        {
            .m_Offset = sizeof(std::uint32_t)*0,
            .m_Format = xgpu::vertex_descriptor::format::USHORT_2D_NORMALIZED   // UV
        }  
        , xgpu::vertex_descriptor::attribute
        {
            .m_Offset = sizeof(std::uint32_t)*1,
            .m_Format = xgpu::vertex_descriptor::format::SHORT_2D_NORMALIZED    // oct normal
        }  
        , xgpu::vertex_descriptor::attribute
        {
            .m_Offset = sizeof(std::uint32_t)*2,
            .m_Format = xgpu::vertex_descriptor::format::SHORT_2D_NORMALIZED}  // oct tangent
    };
    UserData.m_Device.Create(ExtrasDescriptor, { .m_VertexSize = sizeof(xgeom_static::geom::vertex_extras), .m_Attributes = ExtrasAttributes });

    //
    // Time to copy the memory to the right places
    //

    // Upgrade to the runtime version
    xgeom_static::xgpu::geom* pXGPUGeom = static_cast<xgeom_static::xgpu::geom*>(pGeom);

    // Create buffers
    Device.Create(pXGPUGeom->VertexBuffer(),        {.m_Type = xgpu::buffer::type::VERTEX,  .m_Size = pXGPUGeom->getVertices().size_bytes(),        .m_pData = pXGPUGeom->getVertices().data()});
    Device.Create(pXGPUGeom->VertexExtrasBuffer(),  {.m_Type = xgpu::buffer::type::VERTEX,  .m_Size = pXGPUGeom->getVertexExtras().size_bytes(),    .m_pData = pXGPUGeom->getVertexExtras().data() });
    Device.Create(pXGPUGeom->IndexBuffer(),         {.m_Type = xgpu::buffer::type::INDEX,   .m_Size = pXGPUGeom->getIndices().size_bytes(),         .m_pData = pXGPUGeom->getIndices().data()});
    Device.Create(pXGPUGeom->PosPaletteBuffer(),    {.m_Type = xgpu::buffer::type::UNIFORM, .m_Size = pXGPUGeom->getPosPalettes().size_bytes(),     .m_pData = pXGPUGeom->getPosPalettes().data()});
    Device.Create(pXGPUGeom->getUVPalettes(),       {.m_Type = xgpu::buffer::type::UNIFORM, .m_Size = pXGPUGeom->getUVPalettes().size_bytes(),      .m_pData = pXGPUGeom->getUVPalettes().data()});

    // Resolve the default material instances
    for (auto& E : pXGPUGeom->getDefaultMaterialInstances())
    {
        if (auto pRsc = Mgr.getResource(E); pRsc == nullptr )
        {
            assert(false);
        }
    }

    return pXGPUGeom
}

//------------------------------------------------------------------

void xresource::loader< xrsc::geom_type_guid_v >::Destroy(xresource::mgr& Mgr, data_type&& Data, const full_guid& GUID)
{
    auto& UserData = Mgr.getUserData<resource_mgr_user_data>();

    // Release all the buffers
    UserData.m_Device.Destroy(std::move(pXGPUGeom->VertexBuffer()));
    UserData.m_Device.Destroy(std::move(pXGPUGeom->VertexExtrasBuffer()));
    UserData.m_Device.Destroy(std::move(pXGPUGeom->IndexBuffer()));
    UserData.m_Device.Destroy(std::move(pXGPUGeom->PosPaletteBuffer()));
    UserData.m_Device.Destroy(std::move(pXGPUGeom->getUVPalettes()));

    // Release all the material instance references
    for (auto& E : Data.getDefaultMaterialInstances())
    {
        Mgr.ReleaseRef(E);
    }

    // Free the resource
    xserializer::default_memory_handler_v.Free(xserializer::mem_type{ .m_bUnique = true }, &Data);
}
