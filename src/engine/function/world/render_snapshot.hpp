// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_RENDER_SNAPSHOT_HPP_
#define GNEISS_WORLD_RENDER_SNAPSHOT_HPP_

#include "engine/core/error.hpp"
#include "engine/function/render/render_snapshot.hpp"

#include <cstdint>

namespace gneiss::engine {

class world_state;

[[nodiscard]] error build_render_snapshot(world_state& world, std::uint32_t viewport_width,
                                          std::uint32_t viewport_height,
                                          render_snapshot& out_snapshot,
                                          const view_camera* camera = nullptr) noexcept;

} // namespace gneiss::engine

#endif
