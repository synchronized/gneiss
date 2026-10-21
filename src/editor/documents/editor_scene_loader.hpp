// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "commands/editor_command_history.hpp"
#include "documents/scene_document.hpp"
#include "runtime/loading/scene_load_types.hpp"
#include "viewport/editor_camera.hpp"

namespace gneiss::editor {

/** 编辑器所属线程的加载事务；执行器与 Application 由宿主拥有。 */
class editor_scene_loader final {
public:
  result request(engine::application& application, std::string_view uri);
  result advance(engine::application& application, scene_document& session, gneiss_world& world,
                 editor_camera& camera, editor_command_history& history, bool& changed);
  result cancel(engine::application& application);
  void dismiss() noexcept {
    if (!active_) {
      visible_ = false;
    }
  }
  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] result operation() const noexcept { return operation_; }
  [[nodiscard]] std::string_view uri() const noexcept { return uri_; }
  [[nodiscard]] const application_internal::scene_load_progress& progress() const noexcept {
    return progress_;
  }

private:
  std::string uri_;
  application_internal::scene_load_progress progress_;
  std::uint64_t revision_{};
  std::uint64_t request_{};
  result operation_{result::success};
  bool active_{};
  bool visible_{};
  bool queued_{};
};

} // namespace gneiss::editor
