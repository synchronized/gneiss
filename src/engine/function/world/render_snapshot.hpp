// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_RENDER_SNAPSHOT_HPP_
#define GNEISS_WORLD_RENDER_SNAPSHOT_HPP_

#include "engine/core/result.hpp"
#include "engine/function/render/render_snapshot.hpp"
#include <gneiss/engine/world.h>

#include <cstdint>

namespace gneiss::engine::world {

class world_state;

[[nodiscard]] gneiss::engine::core::result
build_render_snapshot(world_state& world, std::uint32_t viewport_width,
                      std::uint32_t viewport_height, engine::render::render_snapshot& out_snapshot,
                      const engine::render::view_camera* camera = nullptr) noexcept;
[[nodiscard]] gneiss::engine::core::result
get_render_snapshot(gneiss_world world, std::uint32_t viewport_width, std::uint32_t viewport_height,
                    engine::render::render_snapshot& out_snapshot,
                    const engine::render::view_camera* camera = nullptr) noexcept;

} // namespace gneiss::engine::world

#endif
