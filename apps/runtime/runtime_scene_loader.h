// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/function/application/application_scene_load_internal.hpp"
#include "ipc_scene_protocol.h"

#include <functional>

namespace gneiss::runtime_internal {
/** 宿主所属线程协调场景事务与模块安全点；调度、资源和 World 仍由 Application 管理。 */
class runtime_scene_loader {
public:
  explicit runtime_scene_loader(gneiss_application application) : application_(application) {}
  [[nodiscard]] gneiss_result request(ipc_scene_request source, std::uint32_t request_id);
  [[nodiscard]] gneiss_result cancel(const ipc_scene_request& source);
  [[nodiscard]] gneiss_result advance();
  std::function<gneiss_result()> before_activate;
  std::function<gneiss_result(const application_internal::scene_load_completion&)> after_activate;
  [[nodiscard]] bool busy() const noexcept { return active_; }
  [[nodiscard]] const ipc_scene_progress& progress() const noexcept { return progress_; }
  [[nodiscard]] std::uint32_t request_id() const noexcept { return request_id_; }
  [[nodiscard]] gneiss_result last_result() const noexcept { return result_; }

private:
  gneiss_result activate(application_internal::scene_load_completion& completion);
  gneiss_application application_{};
  std::uint64_t request_{};
  std::uint32_t request_id_{};
  bool active_{};
  gneiss_result result_{GNEISS_SUCCESS};
  ipc_scene_progress progress_;
};
} // namespace gneiss::runtime_internal
