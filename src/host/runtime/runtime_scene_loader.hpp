// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/error.hpp"
#include "engine/function/asset/asset_batch_request.hpp"
#include "engine/function/asset/asset_request.hpp"
#include "engine/function/scene/scene_update.hpp"
#include "engine/resource/scene.hpp"
#include "ipc_scene_protocol.hpp"
#include <atomic>

#include <functional>
#include <optional>
#include <variant>

namespace gneiss::runtime {
class application;
}

namespace gneiss::host {
/** Runtime 来源重载基线；仅所属线程操作，不保活 World，须先于 Application 释放。 */
struct scene_reload_state {
  engine::world* owner{};
  scene_instance_id instance;
  engine::ref<const engine::scene_content> content;
  std::map<engine::object_address, engine::scene_object_association> objects;
  bool synchronized = true;
};
/** 打开场景后交给宿主绑定检查器的回执，不包含加载进度或退休统计。 */
struct scene_open_result {
  scene_instance_id scene;
  std::vector<engine::scene_object_binding> objects;
  engine::ref<const engine::scene_content> content;
  engine::ref<scene_reload_state> reload_state;
};
struct scene_reload_result {
  engine::error result{engine::error::success};
  std::vector<engine::scene_object_binding> objects;
};

/** 宿主所属线程协调场景加载与模块安全点；调度、资源和 World 仍由 Application 管理。 */
class runtime_scene_loader {
public:
  explicit runtime_scene_loader(::gneiss::runtime::application& application)
      : application_(application) {}
  [[nodiscard]] gneiss::engine::error request(ipc_scene_request source, std::uint32_t request_id);
  [[nodiscard]] gneiss::engine::error cancel(const ipc_scene_request& source);
  [[nodiscard]] gneiss::engine::error advance();
  [[nodiscard]] engine::error reload(engine::ref<scene_reload_state>, std::string_view uri,
                                     bool prefab);
  [[nodiscard]] engine::result<scene_reload_result> take_reload();
  std::function<gneiss::engine::error()> before_activate;
  std::function<gneiss::engine::error(const ::gneiss::host::scene_open_result&)> after_activate;
  [[nodiscard]] bool visual_loading() const noexcept { return bool(visuals_); }
  [[nodiscard]] bool busy() const noexcept { return active_; }
  [[nodiscard]] const ipc_scene_progress& progress() const noexcept { return progress_; }
  [[nodiscard]] std::uint32_t request_id() const noexcept { return request_id_; }
  [[nodiscard]] gneiss::engine::error last_result() const noexcept { return result_; }

private:
  gneiss::engine::error activate(::gneiss::host::scene_open_result& completion);
  engine::error apply_source(scene_reload_state&, engine::ref<const engine::scene_content>);
  void advance_reload();
  struct reload_task {
    engine::ref<scene_reload_state> source;
    std::variant<engine::asset_request<engine::scene>, engine::asset_request<engine::prefab>>
        request;
    std::shared_ptr<std::atomic<bool>> ready;
    std::optional<scene_reload_result> completion;
  };
  std::optional<reload_task> reload_;
  // 借用宿主对象；协调器必须先于 Application 析构。
  ::gneiss::runtime::application& application_;
  engine::asset_request<engine::scene> request_;
  std::shared_ptr<std::atomic<bool>> ready_;
  engine::asset_batch_request visuals_;
  engine::scene_instance current_;
  std::uint32_t request_id_{};
  bool active_{};
  gneiss::engine::error result_{gneiss::engine::error::success};
  ipc_scene_progress progress_;
};
} // namespace gneiss::host
