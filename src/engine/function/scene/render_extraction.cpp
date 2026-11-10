// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/scene/render_extraction.hpp"
#include "engine/function/scene/detail/render_extraction.hpp"
#include "engine/function/scene/detail/world_component_access.hpp"
namespace gneiss::engine {
result<render_snapshot> extract_render_snapshot(world& owner, std::uint32_t width,
                                                std::uint32_t height, const view_camera* camera) {
  auto state = detail::world_component_access::get(owner);
  if (!state)
    return state.error();
  render_snapshot snapshot;
  auto status = build_render_snapshot(**state, width, height, snapshot, camera);
  if (status.failed())
    return status;
  return snapshot;
}

}
