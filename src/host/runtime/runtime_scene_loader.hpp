// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/error.hpp"
#include "engine/function/asset/asset_batch_request.hpp"
#include "engine/function/asset/asset_request.hpp"
#include "ipc_scene_protocol.hpp"
#include "runtime/loading/scene_load_types.hpp"
#include <atomic>

#include <functional>

namespace gneiss::runtime {
class application;
}

namespace gneiss::host {
/** 宿主所属线程协调场景加载与模块安全点；调度、资源和 World 仍由 Application 管理。 */
class runtime_scene_loader {
public:
  explicit runtime_scene_loader(::gneiss::runtime::application& application)
      : application_(application) {}
  [[nodiscard]] gneiss::engine::error request(ipc_scene_request source, std::uint32_t request_id);
  [[nodiscard]] gneiss::engine::error cancel(const ipc_scene_request& source);
  [[nodiscard]] gneiss::engine::error advance();
  std::function<gneiss::engine::error()> before_activate;
  std::function<gneiss::engine::error(const ::gneiss::runtime::scene_load_completion&)>
      after_activate;
  [[nodiscard]] bool visual_loading() const noexcept { return bool(visuals_); }
  [[nodiscard]] bool busy() const noexcept { return active_; }
  [[nodiscard]] const ipc_scene_progress& progress() const noexcept { return progress_; }
  [[nodiscard]] std::uint32_t request_id() const noexcept { return request_id_; }
  [[nodiscard]] gneiss::engine::error last_result() const noexcept { return result_; }

private:
  gneiss::engine::error activate(::gneiss::runtime::scene_load_completion& completion);
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
