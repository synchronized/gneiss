// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <atomic>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/function/asset/asset_batch_request.hpp>
#include <gneiss/engine/function/asset/asset_request.hpp>
#include <gneiss/engine/function/scene/scene_update.hpp>
#include <gneiss/engine/resource/scene.hpp>
#include <gneiss/protocol/ipc_scene_protocol.hpp>

#include <functional>
#include <optional>
#include <variant>

namespace gneiss::runtime {
class application;
}

namespace gneiss::host {
/** Runtime 来源重载基线；仅所属线程操作，不保活 World，须先于 Application 释放。 */
struct scene_reload_state {
  scene_instance instance;
  gneiss::ref<const gneiss::scene_content> content;
  std::map<gneiss::object_address, gneiss::scene_object_association> objects;
  bool synchronized = true;
};
/** 打开场景后交给宿主绑定检查器的回执，不包含加载进度或退休统计。 */
struct scene_open_result {
  scene_instance scene;
  std::vector<gneiss::scene_object_binding> objects;
  gneiss::ref<const gneiss::scene_content> content;
  gneiss::ref<scene_reload_state> reload_state;
};
struct scene_reload_result {
  gneiss::error result{gneiss::error::success};
  std::vector<gneiss::scene_object_binding> objects;
};

/** 宿主所属线程协调场景加载与模块安全点；调度、资源和 World 仍由 Application 管理。 */
class runtime_scene_loader {
public:
  explicit runtime_scene_loader(::gneiss::runtime::application& application)
      : application_(application) {}
  [[nodiscard]] gneiss::error request(ipc_scene_request source, std::uint32_t request_id);
  [[nodiscard]] gneiss::error cancel(const ipc_scene_request& source);
  [[nodiscard]] gneiss::error advance();
  /** 停止宿主时放弃全部加载/重载观察者，并解除业务回调。 */
  void stop();
  [[nodiscard]] gneiss::error reload(gneiss::ref<scene_reload_state>, std::string_view uri,
                                     bool prefab);
  [[nodiscard]] gneiss::result<scene_reload_result> take_reload();
  std::function<gneiss::error()> before_activate;
  std::function<gneiss::error(const ::gneiss::host::scene_open_result&)> after_activate;
  [[nodiscard]] bool visual_loading() const noexcept { return bool(visuals_); }
  [[nodiscard]] bool busy() const noexcept { return active_; }
  [[nodiscard]] const ipc_scene_progress& progress() const noexcept { return progress_; }
  [[nodiscard]] std::uint32_t request_id() const noexcept { return request_id_; }
  [[nodiscard]] gneiss::error last_result() const noexcept { return result_; }

private:
  gneiss::error activate(::gneiss::host::scene_open_result& completion);
  gneiss::error apply_source(scene_reload_state&, gneiss::ref<const gneiss::scene_content>);
  void advance_reload();
  struct reload_task {
    gneiss::ref<scene_reload_state> source;
    std::variant<gneiss::asset_request<gneiss::scene>, gneiss::asset_request<gneiss::prefab>>
        request;
    std::shared_ptr<std::atomic<bool>> ready;
    std::optional<scene_reload_result> completion;
  };
  std::optional<reload_task> reload_;
  // 借用宿主对象；协调器必须先于 Application 析构。
  ::gneiss::runtime::application& application_;
  gneiss::asset_request<gneiss::scene> request_;
  std::shared_ptr<std::atomic<bool>> ready_;
  gneiss::asset_batch_request visuals_;
  gneiss::scene_instance current_;
  std::uint32_t request_id_{};
  bool active_{};
  gneiss::error result_{gneiss::error::success};
  ipc_scene_progress progress_;
};
} // namespace gneiss::host
