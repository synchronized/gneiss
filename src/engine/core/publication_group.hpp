// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace gneiss::engine::world {
class world_state;
}
namespace gneiss::engine::scene {
class scene_tree;
}
namespace gneiss::engine::core {
/** 所属 World 的内部发布门；不持有服务、节点或资产。 */
class publication_group final {
  friend class engine::world::world_state;
  friend class engine::scene::scene_tree;
  explicit publication_group(std::uint32_t domain) : domain_(domain) {}
  std::uint32_t domain_;
  std::size_t count_{};
  bool published_{};
  bool retired_{};
};
using publication = std::shared_ptr<publication_group>;
}
