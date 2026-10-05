// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_RENDER_SNAPSHOT_HPP_
#define GNEISS_WORLD_RENDER_SNAPSHOT_HPP_

#include "render/render_snapshot.hpp"

#include <cstdint>

namespace gneiss::world_internal {

class world_state;

[[nodiscard]] gneiss_result
build_render_snapshot(world_state& world, std::uint32_t viewport_width,
                      std::uint32_t viewport_height,
                      render_internal::render_snapshot& out_snapshot) noexcept;
[[nodiscard]] gneiss_result
get_render_snapshot(gneiss_world world, std::uint32_t viewport_width, std::uint32_t viewport_height,
                    render_internal::render_snapshot& out_snapshot) noexcept;

} // namespace gneiss::world_internal

#endif
