// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace gneiss::world_internal {
class world_state;
}
namespace gneiss::scene_internal {
class scene_tree;
}
namespace gneiss::core {
/** 所属 World 的内部发布门；不持有服务、节点或资产。 */
class publication_group final {
  friend class world_internal::world_state;
  friend class scene_internal::scene_tree;
  explicit publication_group(std::uint32_t domain) : domain_(domain) {}
  std::uint32_t domain_;
  std::size_t count_{};
  bool published_{};
  bool retired_{};
};
using publication = std::shared_ptr<publication_group>;
}
