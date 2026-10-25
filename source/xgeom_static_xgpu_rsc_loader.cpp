#include "xgeom_static_xgpu_runtime.h"
#include "xgeom_static_xgpu_rsc_loader.h"

#include "dependencies/xresource_guid/source/bridges/xresource_xproperty_bridge.h"

//
// We will register the loader, the properties, 
//
inline static auto s_GeomRegistrations = xresource::common_registrations<xrsc::geom_static_type_guid_v>{};


//------------------------------------------------------------------

static
std::size_t MultipleOf64(std::size_t num)
{
    return (num + 63) & ~63;
}

//------------------------------------------------------------------

template<typename T>
std::size_t MakeMuultipleOf64(const T& X)
{
    return X.size();
    auto s = sizeof(X[0]);
    return MultipleOf64(s * X.size())/ s;
}

//------------------------------------------------------------------

xresource::loader< xrsc::geom_static_type_guid_v >::data_type* xresource::loader< xrsc::geom_static_type_guid_v >::Load(xresource::mgr& Mgr, const full_guid& GUID)
{
    auto&                   UserData    = Mgr.getUserData<resource_mgr_user_data>();
    std::wstring            Path        = Mgr.getResourcePath(GUID, type_name_v);
    xgeom_static::geom*     pGeom       = nullptr;

    // Load the xgeom_static. A missing/not-yet-compiled resource is an expected, recoverable case (same
    // reasoning as xtexture_xgpu_rsc_loader.cpp's identical fix: the thumbnail system requests a Load for
    // every geometry merely scrolled into view, or ever named by a stale asset-list entry whose own
    // compile failed) - every caller already handles getResource() returning null, so return null instead
    // of asserting-then-dereferencing a null pGeom below (a crash in a debug build, silent undefined
    // behaviour in Release).
    xserializer::stream Stream;
    if (auto Err = Stream.Load(Path, pGeom); Err)
    {
        return nullptr;
    }

    //
    // Time to copy the memory to the right places
    //

    // Upgrade to the runtime version
    xgeom_static::xgpu::geom* pXGPUGeom = static_cast<xgeom_static::xgpu::geom*>(pGeom);

    // Create buffers
    xgpu::device::error* p;

    // Ideally in the future all these buffer should be a single allocation... 
    0
    ||(p = UserData.m_Device.Create(pXGPUGeom->VertexBuffer(),       xgpu::buffer::setup{.m_Type = xgpu::buffer::type::VERTEX,  .m_EntryByteSize = (int)sizeof(xgeom_static::geom::vertex),            .m_EntryCount = (int)pXGPUGeom->getVertices().size(),     .m_pData = pXGPUGeom->getVertices().data()}))
    ||(p = UserData.m_Device.Create(pXGPUGeom->VertexExtrasBuffer(), xgpu::buffer::setup{.m_Type = xgpu::buffer::type::VERTEX,  .m_EntryByteSize = (int)sizeof(xgeom_static::geom::vertex_extras),     .m_EntryCount = (int)pXGPUGeom->getVertexExtras().size(), .m_pData = pXGPUGeom->getVertexExtras().data()}))
    ||(p = UserData.m_Device.Create(pXGPUGeom->IndexBuffer(),        xgpu::buffer::setup{.m_Type = xgpu::buffer::type::INDEX,   .m_EntryByteSize = (int)sizeof(std::uint16_t),                         .m_EntryCount = (int)pXGPUGeom->getIndices().size(),      .m_pData = pXGPUGeom->getIndices().data()}))
    ||(p = UserData.m_Device.Create(pXGPUGeom->ClusterBuffer(),      xgpu::buffer::setup{.m_Type = xgpu::buffer::type::STORAGE, .m_EntryByteSize = (int)sizeof(xgeom_static::geom::cluster_data),      .m_EntryCount = (int)pXGPUGeom->getClusterData().size(),  .m_pData = pXGPUGeom->getClusterData().data() }))
    ||(pXGPUGeom->getVertexColors().size() && (p = UserData.m_Device.Create(pXGPUGeom->ColorBuffer(), xgpu::buffer::setup{.m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = (int)sizeof(std::uint32_t), .m_EntryCount = (int)pXGPUGeom->getVertexColors().size(), .m_pData = pXGPUGeom->getVertexColors().data()})))
    ||(pXGPUGeom->getUV1s().size() && (p = UserData.m_Device.Create(pXGPUGeom->UV1Buffer(), xgpu::buffer::setup{.m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = (int)sizeof(pXGPUGeom->getUV1s()[0]), .m_EntryCount = (int)pXGPUGeom->getUV1s().size(), .m_pData = pXGPUGeom->getUV1s().data()})))
    ;
    assert(p == nullptr);

    // Resolve the default material instances. A referenced material instance that fails to load
    // (missing/uncompiled itself, or a broken dependency of its own) is the same expected,
    // recoverable case as the geometry's own file above - this slot just stays unresolved, same as
    // an empty ref (see the "no default material" comment above); every consumer already renders a
    // fallback (e.g. the preview's own m_White default) rather than assuming every slot resolved.
    for (auto& E : pXGPUGeom->getDefaultMaterialInstances())
    {
        if (not E.empty())
        {
            (void)Mgr.getResource(E);
        }
        else
        {
            // The user will have to deal with no-default materials...
        }
    }

    // Measurement (XLION_GEOM_SIZES): what the geometry's parts weigh, the GI's derived block by its parts (its sections' blocks one after
    // the other: xlionrender gi::surfel_mesh::UnpackDerived's layout - a header, the read grid's table and lists, two levels of neighbors,
    // the rays' origins)
    if (std::getenv("XLION_GEOM_SIZES"))
    {
        const auto MB = [](std::size_t b) { return double(b) / (1024.0 * 1024.0); };
        const auto& G = *pXGPUGeom;
        std::size_t Table = 0, List = 0, Nbr = 0, Orig = 0, Hdr = 0;
        const auto D = G.getGIDerived();
        const auto Block = [&](std::size_t i, std::size_t End)
        {
            if (i + 8 > End) return;
            const std::uint32_t nTable = D[i + 6], nList = D[i + 7]; Hdr += 8; i += 8;
            Table += 4 * std::size_t(nTable); List += nList; i += 4 * std::size_t(nTable) + nList;
            for (int l = 0; l < 2 && i + 2 <= End; ++l) { const std::uint32_t nF = D[i], nI = D[i + 1]; Nbr += 2 + nF + 2 * std::size_t(nI); i += 2 + nF + 2 * std::size_t(nI); }
            if (i + 2 <= End) Orig += End - i;
        };
        if (G.getGISections().empty()) Block(0, D.size());
        else for (const auto& S : G.getGISections()) Block(S.m_iDerived, std::size_t(S.m_iDerived) + S.m_nDerived);
        const std::size_t nV = G.m_nVertices;
        std::printf("GEOMSIZES %ls: vertices %zu (%.1f MB) extras %.1f MB indices %.1f MB clusters %.1f MB colors/uv1 %.1f MB | GI: surfels %u + parents %u (%.1f MB) vertex surfels %.1f MB materials %.1f MB"
                    " derived %.1f MB (read table %.1f, read lists %.1f, neighbors %.1f, origins %.1f, headers %.2f) sections %u ghosts %u (%.2f MB) | file data %.1f MB\n",
                    Path.c_str(), nV, MB(nV * sizeof(xgeom_static::geom::vertex)), MB(nV * sizeof(xgeom_static::geom::vertex_extras)), MB(G.getIndices().size() * sizeof(std::uint16_t)),
                    MB(G.getClusterData().size() * sizeof(xgeom_static::geom::cluster_data) + G.getClusters().size() * sizeof(xgeom_static::geom::cluster)),
                    MB(G.getVertexColors().size() * 4 + G.getUV1s().size() * 8),
                    G.m_nGISurfels, G.m_nGIParents, MB((std::size_t(G.m_nGISurfels) + G.m_nGIParents) * sizeof(xgeom_static::geom::gi_surfel)), MB(G.getGIVertexSurfels().size() * 2),
                    MB(G.getGISurfelMaterials().size() * sizeof(xgeom_static::geom::gi_surfel_material)), MB(D.size() * 4), MB(Table * 4), MB(List * 4), MB(Nbr * 4), MB(Orig * 4), MB(Hdr * 4),
                    G.m_nGISections, G.m_nGIGhosts, MB(G.getGISections().size() * sizeof(xgeom_static::geom::gi_section) + G.getGIGhosts().size() * sizeof(xgeom_static::geom::gi_ghost) + G.getGIClusterSections().size() * 2),
                    MB(G.m_DataSize));
        std::fflush(stdout);
    }

    return pXGPUGeom;
}

//------------------------------------------------------------------

void xresource::loader< xrsc::geom_static_type_guid_v >::Destroy(xresource::mgr& Mgr, data_type&& Data, const full_guid& GUID)
{
    auto& UserData = Mgr.getUserData<resource_mgr_user_data>();

    // Release all the buffers. They live in the geom's raw runtime memory, which is freed below without running destructors, and
    // xgpu::buffer has no move: Destroy(std::move(B)) queued a copy and left B's reference behind, so all four leaked. Taking the
    // handle out releases it through the device's deferred destroy.
    for (auto* pBuffer : { &Data.VertexBuffer(), &Data.VertexExtrasBuffer(), &Data.IndexBuffer(), &Data.ClusterBuffer(), &Data.ColorBuffer(), &Data.UV1Buffer() })
    {
        xgpu::buffer Released;
        Released.m_Private.swap(pBuffer->m_Private);
    }

    // Release all the material instance references
    for (auto& E : Data.getDefaultMaterialInstances())
    {
        Mgr.ReleaseRef(E);
    }

    // Free the resource
    xserializer::default_memory_handler_v.Free(xserializer::mem_type{ .m_bUnique = true }, &Data);
}
