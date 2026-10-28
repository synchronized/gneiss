// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/error.hpp"
#include "ipc_scene_protocol.hpp"
#include "runtime/loading/scene_load_types.hpp"

#include <functional>

namespace gneiss::runtime {
class application;
}

namespace gneiss::host {
/** 宿主所属线程协调场景事务与模块安全点；调度、资源和 World 仍由 Application 管理。 */
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
  [[nodiscard]] bool busy() const noexcept { return active_; }
  [[nodiscard]] const ipc_scene_progress& progress() const noexcept { return progress_; }
  [[nodiscard]] std::uint32_t request_id() const noexcept { return request_id_; }
  [[nodiscard]] gneiss::engine::error last_result() const noexcept { return result_; }

private:
  gneiss::engine::error activate(::gneiss::runtime::scene_load_completion& completion);
  // 借用宿主对象；协调器必须先于 Application 析构。
  ::gneiss::runtime::application& application_;
  std::uint64_t request_{};
  std::uint32_t request_id_{};
  bool active_{};
  gneiss::engine::error result_{gneiss::engine::error::success};
  ipc_scene_progress progress_;
};
} // namespace gneiss::host
