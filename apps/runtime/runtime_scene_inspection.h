// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_RUNTIME_SCENE_INSPECTION_H_
#define GNEISS_APPS_RUNTIME_RUNTIME_SCENE_INSPECTION_H_

#include "ipc_inspection_protocol.h"

#include <gneiss/application.h>
#include <gneiss/scene.h>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace gneiss::runtime_internal {

inline constexpr std::size_t runtime_inspection_max_nodes = 4096U;
inline constexpr std::size_t runtime_inspection_max_changes = runtime_inspection_max_nodes * 2U;
inline constexpr std::size_t runtime_inspection_max_string_size = 16U * 1024U;

struct runtime_scene_source_node final {
  std::uint64_t native_node = 0U;
  std::uint64_t native_parent = 0U;
  std::string uuid;
  std::string name;
  gneiss_transform local_transform = GNEISS_TRANSFORM_IDENTITY;
  std::uint32_t component_flags = 0U;
  gneiss_camera_desc camera = GNEISS_CAMERA_DESC_INIT;
  std::string mesh_uri;
  std::string material_uri;
};

using runtime_scene_snapshot_node = ipc_inspection_node;
using runtime_scene_change_type = ipc_inspection_change_type;
using runtime_scene_change = ipc_inspection_change;
using runtime_scene_snapshot = ipc_inspection_batch;

/** 仅由 Runtime 主线程调用，将场景状态折叠为确定性的完整快照或增量。 */
class runtime_scene_inspection final {
public:
  explicit runtime_scene_inspection(std::uint64_t session_id) noexcept;

  [[nodiscard]] result capture(std::span<const runtime_scene_source_node> nodes, bool force_full,
                               runtime_scene_snapshot& output) noexcept;
  [[nodiscard]] result capture_scene(gneiss_application application, gneiss_scene_instance scene,
                                     bool force_full, runtime_scene_snapshot& output) noexcept;
  void reset(std::uint64_t session_id) noexcept;

private:
  std::uint64_t session_id_ = 0U;
  std::uint64_t next_sequence_ = 1U;
  std::uint64_t next_object_value_ = 1U;
  std::map<std::string, ipc_runtime_object_id, std::less<>> identities_;
  std::map<std::uint64_t, std::uint32_t> generations_;
  std::vector<std::uint64_t> free_values_;
  std::map<std::uint64_t, runtime_scene_snapshot_node> previous_;
  bool is_initialized_ = false;
};

} // namespace gneiss::runtime_internal

#endif
