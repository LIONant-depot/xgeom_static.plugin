#ifndef XGEOM_STATIC_XGPU_RUNTIME
#define XGEOM_STATIC_XGPU_RUNTIME
#pragma once

#include "xgpu.h"
#include "xgeom_static.h"

namespace xgeom_static::xgpu
{
    struct geom : xgeom_static::geom
    {
        inline constexpr auto vertex_buffer_offset_v        = 0                             + 0;
        inline constexpr auto vertex_extras_buffer_offset_v = vertex_buffer_offset_v        + sizeof(xgpu::buffer);
        inline constexpr auto index_buffer_offset_v         = vertex_extras_buffer_offset_v + sizeof(xgpu::buffer);
        inline constexpr auto pos_pal_buffer_offset_v       = index_buffer_offset_v         + sizeof(xgpu::buffer);
        inline constexpr auto uv_pal_buffer_offset_v        = pos_pal_buffer_offset_v       + sizeof(xgpu::buffer);
        inline constexpr auto runtime_consumed_v            = uv_pal_buffer_offset_v        + sizeof(xgpu::buffer);
        static_assert(sizeof(xgeom_static::geom::runtime_allocation) == runtime_consumed_v );

        inline auto& VertexBuffer         (void) noexcept { return reinterpret_cast<xgpu::buffer&>(this->m_RunTimeSpace[vertex_buffer_offset_v        / sizeof(std::size_t)]); }
        inline auto& VertexExtrasBuffer   (void) noexcept { return reinterpret_cast<xgpu::buffer&>(this->m_RunTimeSpace[vertex_extras_buffer_offset_v / sizeof(std::size_t)]); }
        inline auto& IndexBuffer          (void) noexcept { return reinterpret_cast<xgpu::buffer&>(this->m_RunTimeSpace[index_buffer_offset_v         / sizeof(std::size_t)]); }
        inline auto& PosPaletteBuffer     (void) noexcept { return reinterpret_cast<xgpu::buffer&>(this->m_RunTimeSpace[pos_pal_buffer_offset_v       / sizeof(std::size_t)]); }
        inline auto& UVPaletteBuffer      (void) noexcept { return reinterpret_cast<xgpu::buffer&>(this->m_RunTimeSpace[uv_pal_buffer_offset_v        / sizeof(std::size_t)]); }
    };

} // namespace xgeom_static::xgpu

#endif