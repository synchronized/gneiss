// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_RUNTIME_SCENE_INSPECTION_H_
#define GNEISS_APPS_RUNTIME_RUNTIME_SCENE_INSPECTION_H_

#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/math/camera.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/resource/scene/component_description.hpp"
#include "ipc_inspection_protocol.hpp"

#include "engine/core/rid.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace gneiss::runtime {
class application;
}
namespace gneiss::host {

inline constexpr std::size_t runtime_inspection_max_nodes = 4096U;
inline constexpr std::size_t runtime_inspection_max_changes = runtime_inspection_max_nodes * 2U;
inline constexpr std::size_t runtime_inspection_max_string_size = 16U * 1024U;

struct runtime_scene_source_node final {
  std::uint64_t native_node = 0U;
  std::uint64_t native_parent = 0U;
  std::uint64_t native_entity = UINT64_C(0);
  gneiss::engine::guid uuid;
  gneiss::engine::guid prefab_instance_uuid;
  gneiss::engine::guid prefab_source_node_uuid;
  std::string name;
  gneiss::engine::math::transform local_transform;
  gneiss::engine::component_descriptions components{};
};

using runtime_scene_snapshot_node = ipc_inspection_node;
using runtime_scene_change_type = ipc_inspection_change_type;
using runtime_scene_change = ipc_inspection_change;
using runtime_scene_snapshot = ipc_inspection_batch;

/** 仅由 Runtime 主线程调用，将场景状态折叠为确定性的完整快照或增量。 */
class runtime_scene_inspection final {
public:
  explicit runtime_scene_inspection(std::uint64_t session_id) noexcept;

  [[nodiscard]] gneiss::engine::error capture(std::span<const runtime_scene_source_node> nodes,
                                              bool force_full,
                                              runtime_scene_snapshot& output) noexcept;
  [[nodiscard]] gneiss::engine::error capture_scene(::gneiss::runtime::application& application,
                                                    gneiss::scene_instance_id scene,
                                                    bool force_full,
                                                    runtime_scene_snapshot& output) noexcept;
  [[nodiscard]] gneiss::engine::error resolve_entity(ipc_runtime_object_id object,
                                                     std::uint64_t& output) const noexcept;
  void reset(std::uint64_t session_id) noexcept;

private:
  std::uint64_t session_id_ = 0U;
  std::uint64_t next_sequence_ = 1U;
  std::uint64_t next_object_value_ = 1U;
  std::map<std::array<engine::guid, 3>, ipc_runtime_object_id> identities_;
  std::map<std::uint64_t, std::uint32_t> generations_;
  std::vector<std::uint64_t> free_values_;
  std::map<std::uint64_t, runtime_scene_snapshot_node> previous_;
  std::map<std::uint64_t, std::pair<ipc_runtime_object_id, std::uint64_t>> entities_;
  bool is_initialized_ = false;
};

} // namespace gneiss::host

#endif
