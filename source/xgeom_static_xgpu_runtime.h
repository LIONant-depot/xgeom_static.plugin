#ifndef XGEOM_STATIC_XGPU_RUNTIME
#define XGEOM_STATIC_XGPU_RUNTIME
#pragma once

#include "source/xgpu.h"
#include "xgeom_static.h"

namespace xgeom_static::xgpu
{
    struct geom : xgeom_static::geom
    {
        inline static constexpr auto index_buffer_offset_v              = 0                                 + 0;
        inline static constexpr auto vertex_buffer_offset_v             = index_buffer_offset_v             + sizeof(::xgpu::buffer);
        inline static constexpr auto vertex_extras_buffer_offset_v      = vertex_buffer_offset_v            + sizeof(::xgpu::buffer);
        inline static constexpr auto cluster_structs_buffer_offset_v    = vertex_extras_buffer_offset_v     + sizeof(::xgpu::buffer);
        inline static constexpr auto runtime_consumed_v                 = cluster_structs_buffer_offset_v   + sizeof(::xgpu::buffer);
        static_assert(sizeof(xgeom_static::geom::runtime_allocation) == runtime_consumed_v );

        inline auto& VertexBuffer         (void) noexcept { return reinterpret_cast<::xgpu::buffer&>(this->m_RunTimeSpace[vertex_buffer_offset_v            / sizeof(std::size_t)]); }
        inline auto& VertexExtrasBuffer   (void) noexcept { return reinterpret_cast<::xgpu::buffer&>(this->m_RunTimeSpace[vertex_extras_buffer_offset_v     / sizeof(std::size_t)]); }
        inline auto& IndexBuffer          (void) noexcept { return reinterpret_cast<::xgpu::buffer&>(this->m_RunTimeSpace[index_buffer_offset_v             / sizeof(std::size_t)]); }
        inline auto& ClusterBuffer        (void) noexcept { return reinterpret_cast<::xgpu::buffer&>(this->m_RunTimeSpace[cluster_structs_buffer_offset_v   / sizeof(std::size_t)]); }
    };

} // namespace xgeom_static::xgpu

#endif