#include "dependencies/xLIONRender/src/xlionrender_env.h"      // (the environment switches: one answer to MSVC's C4996, header only)
#include "xgeom_static_xgpu_runtime.h"
#include "xgeom_static_xgpu_rsc_loader.h"

#include "dependencies/xresource_guid/source/bridges/xresource_xproperty_bridge.h"
#include <mutex>

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
// Residency (geom_streaming_plan.md; the textures' pattern: xtexture_residency). The compiler emits the LODs coarsest first, so the
// vertex, index, colour and UV1 arrays end with LOD 0's clusters: the GPU buffers hold the prefix down to the finest LOD wanted. The
// geometry's data stays in RAM (the GI's CPU structures read LOD 0: plan option a), so a change is immediate - new buffers from it
// replacing the old ones (the device's deferred destroy). The renderer finds what is resident from the index buffer's size
// (xlionrender::cluster_geom::Refresh). Requests from scripts (GeomResidency) and XGEOM_STATIC_FINEST_LOD; by distance later (as P4).
//------------------------------------------------------------------
namespace
{
    struct geom_residency
    {
        xgeom_static::xgpu::geom*   m_pGeom     = nullptr;
        xresource::full_guid        m_Guid      = {};
        int                         m_Finest    = 0;        // the finest LOD resident
    };
    struct geom_residency_state
    {
        std::mutex                  m_Lock;
        std::vector<geom_residency> m_Geoms;
    };
    inline geom_residency_state s_GeomResidency;

    // The prefix (vertices, indices) holding every LOD from Finest on (a mesh with fewer: its coarsest)
    std::pair<std::size_t, std::size_t> GeomPrefixFor(const xgeom_static::geom& G, int Finest) noexcept
    {
        std::size_t nV = 0, nI = 0;
        for (const auto& M : G.getMeshes())
            for (int k = std::min(Finest, int(M.m_nLODs) - 1); k < int(M.m_nLODs); ++k)
            {
                const auto& L = G.getLODs()[M.m_iLOD + k];
                for (const auto& S : G.getSubmeshes().subspan(L.m_iSubmesh, L.m_nSubmesh))
                    for (std::uint32_t c = S.m_iCluster; c < std::uint32_t(S.m_iCluster) + S.m_nCluster && c < G.getClusters().size(); ++c)
                    {
                        const auto& C = G.getClusters()[c];
                        nV = std::max<std::size_t>(nV, std::size_t(C.m_iVertex) + C.m_nVertices);
                        nI = std::max<std::size_t>(nI, std::size_t(C.m_iIndex) + C.m_nIndices);
                    }
            }
        return { nV, nI };
    }

    // The vertex streams and the indices for the prefix (the cluster data: all of it, small)
    xgpu::device::error* MakeGeomStreams(xgpu::device& Device, xgeom_static::xgpu::geom& G, std::size_t nV, std::size_t nI) noexcept
    {
        const auto One = [&](xgpu::buffer& Out, xgpu::buffer::type Type, int Size, std::size_t n, const void* pData, const char* pName) -> xgpu::device::error*
        {
            if (n == 0) return nullptr;
            xgpu::buffer B;
            if (auto Err = Device.Create(B, xgpu::buffer::setup{ .m_Type = Type, .m_EntryByteSize = Size, .m_EntryCount = int(n), .m_pData = const_cast<void*>(pData), .m_pName = pName }); Err) return Err;
            Out.m_Private.swap(B.m_Private);        // (the old buffer: B's destructor, through the deferred destroy - a frame in flight may still read it)
            return nullptr;
        };
        xgpu::device::error* p = nullptr;
        0
        || (p = One(G.VertexBuffer(),       xgpu::buffer::type::VERTEX, int(sizeof(xgeom_static::geom::vertex)),        nV, G.getVertices().data(),     "Geometry: vertices"))
        || (p = One(G.VertexExtrasBuffer(), xgpu::buffer::type::VERTEX, int(sizeof(xgeom_static::geom::vertex_extras)), nV, G.getVertexExtras().data(), "Geometry: vertex extras"))
        || (p = One(G.IndexBuffer(),        xgpu::buffer::type::INDEX,  int(sizeof(std::uint16_t)),                     nI, G.getIndices().data(),      "Geometry: indices"))
        || (p = One(G.ColorBuffer(),        xgpu::buffer::type::VERTEX, int(sizeof(std::uint32_t)),                     G.getVertexColors().empty() ? 0 : nV, G.getVertexColors().data(), "Geometry: colours"))
        || (p = One(G.UV1Buffer(),          xgpu::buffer::type::VERTEX, int(sizeof(G.getUV1s()[0])),                    G.getUV1s().empty() ? 0 : nV, G.getUV1s().data(), "Geometry: uv1"))
        ;
        return p;
    }

    // Finest wanted on the GPU (clamped to what saves anything: a geometry whose LOD 0 is not a suffix keeps it all)
    void ApplyGeomResidency(xgpu::device& Device, geom_residency& R, int Finest) noexcept
    {
        auto& G = *R.m_pGeom;
        int nLODs = 1;
        for (const auto& M : G.getMeshes()) nLODs = std::max(nLODs, int(M.m_nLODs));
        Finest = std::clamp(Finest, 0, nLODs - 1);
        auto [nV, nI] = GeomPrefixFor(G, Finest);
        if (Finest > 0 && nI >= G.getIndices().size()) { Finest = 0; nV = G.getVertices().size(); nI = G.getIndices().size(); }      // (LOD 0 not last: an older compile)
        if (Finest == R.m_Finest && G.IndexBuffer().m_Private && std::size_t(G.IndexBuffer().getEntryCount()) == nI) return;
        if (MakeGeomStreams(Device, G, nV, nI) == nullptr) R.m_Finest = Finest;
    }
}

namespace xgeom_static_residency
{
    void Request(xresource::mgr& Mgr, const xresource::full_guid& Guid, int FinestLOD) noexcept
    {
        auto& Device = Mgr.getUserData<resource_mgr_user_data>().m_Device;
        std::lock_guard Lk(s_GeomResidency.m_Lock);
        for (auto& R : s_GeomResidency.m_Geoms) if (R.m_Guid == Guid) ApplyGeomResidency(Device, R, FinestLOD);
    }
    void RequestAll(xresource::mgr& Mgr, int FinestLOD) noexcept
    {
        auto& Device = Mgr.getUserData<resource_mgr_user_data>().m_Device;
        std::lock_guard Lk(s_GeomResidency.m_Lock);
        for (auto& R : s_GeomResidency.m_Geoms) ApplyGeomResidency(Device, R, FinestLOD);
    }
    stats getStats(const xresource::mgr&) noexcept
    {
        stats S;
        std::lock_guard Lk(s_GeomResidency.m_Lock);
        for (const auto& R : s_GeomResidency.m_Geoms)
        {
            const auto& G = *R.m_pGeom;
            const std::size_t PerVertex = sizeof(xgeom_static::geom::vertex) + sizeof(xgeom_static::geom::vertex_extras) + (G.getVertexColors().empty() ? 0 : 4) + (G.getUV1s().empty() ? 0 : sizeof(G.getUV1s()[0]));
            const auto Bytes = [&](std::size_t nV, std::size_t nI) { return std::uint64_t(nV * PerVertex + nI * sizeof(std::uint16_t)); };
            const auto [nV, nI] = GeomPrefixFor(G, R.m_Finest);
            ++S.m_nGeoms;
            S.m_nReduced  += R.m_Finest > 0;
            S.m_FullBytes += Bytes(G.getVertices().size(), G.getIndices().size());
            S.m_Bytes     += R.m_Finest > 0 ? Bytes(nV, nI) : Bytes(G.getVertices().size(), G.getIndices().size());
        }
        return S;
    }
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
    ||(p = UserData.m_Device.Create(pXGPUGeom->VertexBuffer(),       xgpu::buffer::setup{.m_Type = xgpu::buffer::type::VERTEX,  .m_EntryByteSize = (int)sizeof(xgeom_static::geom::vertex),            .m_EntryCount = (int)pXGPUGeom->getVertices().size(),     .m_pData = pXGPUGeom->getVertices().data(), .m_pName = "Geometry: vertices"}))
    ||(p = UserData.m_Device.Create(pXGPUGeom->VertexExtrasBuffer(), xgpu::buffer::setup{.m_Type = xgpu::buffer::type::VERTEX,  .m_EntryByteSize = (int)sizeof(xgeom_static::geom::vertex_extras),     .m_EntryCount = (int)pXGPUGeom->getVertexExtras().size(), .m_pData = pXGPUGeom->getVertexExtras().data(), .m_pName = "Geometry: vertex extras"}))
    ||(p = UserData.m_Device.Create(pXGPUGeom->IndexBuffer(),        xgpu::buffer::setup{.m_Type = xgpu::buffer::type::INDEX,   .m_EntryByteSize = (int)sizeof(std::uint16_t),                         .m_EntryCount = (int)pXGPUGeom->getIndices().size(),      .m_pData = pXGPUGeom->getIndices().data(), .m_pName = "Geometry: indices"}))
    ||(p = UserData.m_Device.Create(pXGPUGeom->ClusterBuffer(),      xgpu::buffer::setup{.m_Type = xgpu::buffer::type::STORAGE, .m_EntryByteSize = (int)sizeof(xgeom_static::geom::cluster_data),      .m_EntryCount = (int)pXGPUGeom->getClusterData().size(),  .m_pData = pXGPUGeom->getClusterData().data(), .m_pName = "Geometry: clusters" }))
    ||(pXGPUGeom->getVertexColors().size() && (p = UserData.m_Device.Create(pXGPUGeom->ColorBuffer(), xgpu::buffer::setup{.m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = (int)sizeof(std::uint32_t), .m_EntryCount = (int)pXGPUGeom->getVertexColors().size(), .m_pData = pXGPUGeom->getVertexColors().data(), .m_pName = "Geometry: colours"})))
    ||(pXGPUGeom->getUV1s().size() && (p = UserData.m_Device.Create(pXGPUGeom->UV1Buffer(), xgpu::buffer::setup{.m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = (int)sizeof(pXGPUGeom->getUV1s()[0]), .m_EntryCount = (int)pXGPUGeom->getUV1s().size(), .m_pData = pXGPUGeom->getUV1s().data(), .m_pName = "Geometry: uv1"})))
    ;
    assert(p == nullptr);

    // Its residency (the GPU's prefix: XGEOM_STATIC_FINEST_LOD - a test's or a low-memory run's - else all of it)
    {
        static const int s_Finest = [] { const char* p = xlionrender::Env("XGEOM_STATIC_FINEST_LOD"); return p ? std::max(0, std::atoi(p)) : 0; }();
        std::lock_guard Lk(s_GeomResidency.m_Lock);
        auto& R = s_GeomResidency.m_Geoms.emplace_back(geom_residency{ pXGPUGeom, GUID, 0 });
        if (s_Finest > 0) ApplyGeomResidency(UserData.m_Device, R, s_Finest);
    }

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
    if (xlionrender::Env("XLION_GEOM_SIZES"))
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
    {
        std::lock_guard Lk(s_GeomResidency.m_Lock);
        std::erase_if(s_GeomResidency.m_Geoms, [&](const geom_residency& R) { return R.m_pGeom == &Data; });
    }

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
