// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/guid.hpp"
#include "runtime_object_id.hpp"

#include "engine/core/error.hpp"
#include "engine/core/math/camera.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/rid.hpp"

#include <string>
#include <vector>

namespace gneiss::editor {

/** 检查增量的会话与顺序标记。序号从 1 开始并在会话内严格递增。 */
struct runtime_scene_stamp final {
  std::uint64_t session_id = 0U;
  std::uint64_t sequence = 0U;
};

enum class runtime_scene_sequence_result : std::uint8_t {
  accepted,
  duplicate,
  gap,
  stale_session,
  invalid,
};

/** Editor 侧检查消息顺序跟踪器；发现缺口后由上层请求完整快照。 */
class runtime_scene_sequence_tracker final {
public:
  [[nodiscard]] gneiss::engine::error begin(std::uint64_t session_id,
                                            std::uint64_t first_sequence = 1U) noexcept;
  void reset() noexcept;
  [[nodiscard]] runtime_scene_sequence_result observe(runtime_scene_stamp stamp) noexcept;
  [[nodiscard]] std::uint64_t session_id() const noexcept { return session_id_; }
  [[nodiscard]] std::uint64_t next_sequence() const noexcept { return next_sequence_; }

private:
  std::uint64_t session_id_ = 0U;
  std::uint64_t next_sequence_ = 0U;
};

enum class runtime_scene_change_type : std::uint8_t { upsert, remove };

struct runtime_scene_node final {
  runtime_object_id id;
  runtime_object_id parent;
  gneiss::engine::guid uuid;
  gneiss::engine::guid prefab_instance_uuid;
  gneiss::engine::guid prefab_source_node_uuid;
  std::string name;
  gneiss::engine::math::transform local_transform;
  std::uint32_t component_flags = 0U;
  gneiss::engine::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  std::string mesh_uri;
  std::string material_uri;
};

struct runtime_scene_change final {
  runtime_scene_change_type type = runtime_scene_change_type::upsert;
  runtime_object_id id;
  runtime_scene_node node;
};

struct runtime_scene_batch final {
  runtime_scene_stamp stamp;
  bool is_full = false;
  std::uint32_t chunk_index = 0U;
  std::uint32_t chunk_count = 1U;
  std::vector<runtime_scene_change> changes;
};

} // namespace gneiss::editor
