// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "documents/document_editing.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/loading/scene_load_types.hpp"
#include "viewport/editor_camera.hpp"

namespace gneiss::editor {

/** 编辑器所属线程的加载事务；执行器与 Application 由宿主拥有。 */
class editor_scene_loader final {
public:
  gneiss::engine::result request(runtime::application& application, const document_editing& editing,
                                 std::string_view uri);
  gneiss::engine::result advance(runtime::application& application, document_editing& editing,
                                 gneiss::engine::world*& world, editor_camera& camera,
                                 bool& changed);
  gneiss::engine::result cancel(runtime::application& application);
  void dismiss() noexcept {
    if (!active_) {
      visible_ = false;
    }
  }
  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] gneiss::engine::result operation() const noexcept { return operation_; }
  [[nodiscard]] std::string_view uri() const noexcept { return uri_; }
  [[nodiscard]] const runtime::scene_load_progress& progress() const noexcept { return progress_; }

private:
  std::string uri_;
  runtime::scene_load_progress progress_;
  std::uint64_t revision_{};
  std::uint64_t request_{};
  gneiss::engine::result operation_{gneiss::engine::result::success};
  bool active_{};
  bool visible_{};
  bool queued_{};
};

} // namespace gneiss::editor
