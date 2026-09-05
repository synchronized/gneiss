// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_STRUCTURAL_DIFF_H_
#define GNEISS_SCENE_STRUCTURAL_DIFF_H_

#include "scene/scene_description.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gneiss::scene_internal {

enum class structural_change : std::uint32_t {
  none = 0U,
  name = 1U << 0U,
  parent = 1U << 1U,
  transform = 1U << 2U,
  camera = 1U << 3U,
  mesh_renderer = 1U << 4U,
};

[[nodiscard]] constexpr structural_change operator|(structural_change left,
                                                    structural_change right) noexcept {
  return static_cast<structural_change>(static_cast<std::uint32_t>(left) |
                                        static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr bool has_change(structural_change changes,
                                        structural_change expected) noexcept {
  return (static_cast<std::uint32_t>(changes) & static_cast<std::uint32_t>(expected)) != 0U;
}

struct structural_node_add final {
  std::string uuid;
  std::size_t new_index = 0U;
  std::size_t depth = 0U;
};

struct structural_node_remove final {
  std::string uuid;
  std::size_t old_index = 0U;
  std::size_t depth = 0U;
};

struct structural_node_update final {
  std::string uuid;
  std::size_t old_index = 0U;
  std::size_t new_index = 0U;
  structural_change changes{structural_change::none};
};

/** 不接触 World，根据稳定 UUID 生成确定排序的结构差异。 */
struct structural_diff final {
  /** 父节点先于子节点。 */
  std::vector<structural_node_add> added;
  /** 按 UUID 排序；提交时应在删除旧父节点前应用重挂接。 */
  std::vector<structural_node_update> updated;
  /** 子节点先于父节点。 */
  std::vector<structural_node_remove> removed;

  [[nodiscard]] bool empty() const noexcept {
    return added.empty() && updated.empty() && removed.empty();
  }
};

[[nodiscard]] gneiss_result
build_structural_diff(const std::vector<object_description>& old_objects,
                      const std::vector<object_description>& new_objects,
                      structural_diff& output) noexcept;

} // namespace gneiss::scene_internal

#endif
