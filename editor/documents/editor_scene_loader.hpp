// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "documents/document_editing.hpp"
#include "ui/viewport/editor_camera.hpp"
#include <atomic>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/function/asset/asset_batch_request.hpp>
#include <gneiss/engine/function/asset/asset_request.hpp>
#include <gneiss/engine/function/scene/world.hpp>

namespace gneiss::editor {

/** 编辑器打开文档的可观察状态；内存实例化在一次更新中完成。 */
enum class scene_open_state { idle, loading, opened, failed, cancelled };

/** 编辑器所属线程的场景加载协调；执行器与 Application 由宿主拥有。 */
class editor_scene_loader final {
public:
  gneiss::error request(app::application& application, const document_editing& editing,
                        std::string_view uri);
  gneiss::error advance(app::application& application, document_editing& editing,
                        gneiss::world& world, editor_camera& camera, bool& changed);
  gneiss::error cancel(app::application& application);
  void dismiss() noexcept {
    if (!active()) {
      visible_ = false;
    }
  }
  [[nodiscard]] bool visual_loading() const noexcept { return bool(visuals_); }
  [[nodiscard]] bool active() const noexcept { return state_ == scene_open_state::loading; }
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] gneiss::error operation() const noexcept { return operation_; }
  [[nodiscard]] std::string_view uri() const noexcept { return uri_; }
  [[nodiscard]] scene_open_state state() const noexcept { return state_; }

private:
  std::string uri_;
  scene_open_state state_{scene_open_state::idle};
  gneiss::asset_request<gneiss::scene> request_;
  std::shared_ptr<std::atomic<bool>> ready_;
  gneiss::asset_batch_request visuals_;
  gneiss::error operation_{gneiss::error::success};
  bool visible_{};
  bool queued_{};
};

} // namespace gneiss::editor
