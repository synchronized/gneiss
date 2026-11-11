// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "documents/document_editing.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/asset/asset_batch_request.hpp"
#include "engine/function/asset/asset_request.hpp"
#include "engine/function/scene/world.hpp"
#include "runtime/loading/scene_load_types.hpp"
#include "viewport/editor_camera.hpp"
#include <atomic>

namespace gneiss::editor {

/** 编辑器所属线程的场景加载协调；执行器与 Application 由宿主拥有。 */
class editor_scene_loader final {
public:
  gneiss::engine::error request(runtime::application& application, const document_editing& editing,
                                std::string_view uri);
  gneiss::engine::error advance(runtime::application& application, document_editing& editing,
                                gneiss::engine::world*& world, editor_camera& camera,
                                bool& changed);
  gneiss::engine::error cancel(runtime::application& application);
  void dismiss() noexcept {
    if (!active_) {
      visible_ = false;
    }
  }
  [[nodiscard]] bool visual_loading() const noexcept { return bool(visuals_); }
  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] gneiss::engine::error operation() const noexcept { return operation_; }
  [[nodiscard]] std::string_view uri() const noexcept { return uri_; }
  [[nodiscard]] const runtime::scene_load_progress& progress() const noexcept { return progress_; }

private:
  std::string uri_;
  runtime::scene_load_progress progress_;
  engine::asset_request<engine::scene> request_;
  std::shared_ptr<std::atomic<bool>> ready_;
  engine::asset_batch_request visuals_;
  gneiss::engine::error operation_{gneiss::engine::error::success};
  bool active_{};
  bool visible_{};
  bool queued_{};
};

} // namespace gneiss::editor
