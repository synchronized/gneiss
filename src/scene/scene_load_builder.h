// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "scene/scene_instance_service.h"
#include "scene/scene_load_preparation.h"

#include <chrono>
#include <unordered_map>

namespace gneiss::scene_internal {

/** 所属线程推进不可见候选域。调用方必须先准备全部渲染依赖，并保持服务与 World 存活。
 * 描述安装、节点和覆盖各是一个可暂停工作项；不执行文件 I/O，不自动激活 Application。 */
class scene_load_builder final {
public:
  scene_load_builder(scene_instance_service& service, prepared_scene_description prepared);
  [[nodiscard]] gneiss_result
  advance(bool& complete, std::size_t maximum_steps = 64U,
          std::chrono::nanoseconds maximum_time = std::chrono::milliseconds(2)) noexcept;
  [[nodiscard]] std::size_t completed_nodes() const noexcept { return completed_nodes_; }
  [[nodiscard]] gneiss_scene_instance instance() const noexcept { return handle_; }

private:
  [[nodiscard]] gneiss_result step();
  [[nodiscard]] gneiss_result step_prefab();
  scene_instance_service& service_;
  prepared_scene_description prepared_;
  std::unique_ptr<scene_instance> instance_;
  std::map<std::string, prefab_asset_lease> prefabs_;
  std::unordered_map<std::string, gneiss_scene_node_id> nodes_;
  std::unordered_map<std::string, gneiss_scene_node_id> prefab_nodes_;
  std::unique_ptr<prefab_runtime_instance> prefab_;
  std::size_t object_cursor_{};
  std::size_t prefab_cursor_{};
  std::size_t prefab_node_cursor_{};
  std::size_t override_cursor_{};
  std::size_t completed_nodes_{};
  gneiss_scene_instance handle_{};
  gneiss_result failure_{GNEISS_SUCCESS};
};

} // namespace gneiss::scene_internal
