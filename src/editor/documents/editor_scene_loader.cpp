// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/editor_scene_loader.hpp"
#include "application/application.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/world.hpp"

namespace gneiss::editor {

gneiss::engine::error editor_scene_loader::request(runtime::application& application,
                                                   const document_editing& editing,
                                                   std::string_view uri) {
  if (active_ || editing.active()) {
    return gneiss::engine::error::not_ready;
  }
  if (!application.accepts_requests() || !application.is_owner_thread() || uri.empty()) {
    return gneiss::engine::error::invalid_argument;
  }
  uri_ = uri;
  progress_ = {};
  operation_ = gneiss::engine::error::success;
  active_ = true;
  visible_ = true;
  queued_ = true;
  return gneiss::engine::error::success;
}

gneiss::engine::error editor_scene_loader::cancel(runtime::application& application) {
  if (!active_) {
    return gneiss::engine::error::not_ready;
  }
  if (queued_) {
    queued_ = false;
    active_ = false;
    operation_ = gneiss::engine::error::invalid_state;
    progress_.phase = runtime::scene_load_phase::cancelled;
    progress_.cleanup_complete = true;
    return gneiss::engine::error::success;
  }
  if (!application.accepts_requests() || !application.is_owner_thread())
    return engine::error::invalid_state;
  const auto operation = request_.cancel();
  if (operation == gneiss::engine::error::success) {
    progress_.can_cancel = false;
  }
  return operation;
}

gneiss::engine::error editor_scene_loader::advance(runtime::application& application,
                                                   document_editing& editing,
                                                   gneiss::engine::world*& world,
                                                   editor_camera& camera, bool& changed) {
  using namespace runtime;
  changed = false;
  // 防御非 UI 调用方：有编辑未结束时不切换场景，避免使编辑绑定失效。
  if (editing.active())
    return gneiss::engine::error::success;
  scene_load_completion completion;
  bool finished{};
  if (active_ && !queued_) {
    auto terminal = request_.take();
    if (terminal) {
      completion = std::move(*terminal);
      finished = true;
    } else if (terminal.error() != engine::error::not_ready) {
      return terminal.error();
    }
  }
  if (!active_) {
    return gneiss::engine::error::success;
  }
  if (queued_) {
    // 在更新阶段提交请求，菜单回调本身不做加载工作。
    queued_ = false;
    auto submitted = application.scenes().open(uri_);
    request_ = submitted ? std::move(*submitted) : runtime::scene_open_request{};
    operation_ =
        submitted ? gneiss::engine::error{gneiss::engine::error::success} : submitted.error();
    active_ = operation_ == gneiss::engine::error::success;
    if (!active_) {
      progress_.phase = scene_load_phase::failed;
    } else {
      progress_.can_cancel = true;
    }
    return gneiss::engine::error::success;
  }
  if (finished) {
    progress_ = completion.progress;
    operation_ = completion.result;
    active_ = progress_.cleanup_pending;
    return gneiss::engine::error::success;
  }
  auto observed = request_.progress();
  if (!observed) {
    return gneiss::engine::error::success;
  }
  progress_ = *observed;
  if (progress_.cleanup_complete && progress_.phase == scene_load_phase::cancelled) {
    active_ = false;
    return gneiss::engine::error::success;
  }
  if (progress_.phase != scene_load_phase::ready) {
    return gneiss::engine::error::success;
  }
  auto activated = application.scenes().activate(std::move(request_));
  if (!activated) {
    return activated.error() == gneiss::engine::error::not_ready
               ? gneiss::engine::error{gneiss::engine::error::success}
               : activated.error();
  }
  completion = std::move(*activated);

  // 场景切换保持 World 不变；释放旧场景的编辑绑定后采用新的实例。
  const auto closed = editing.close();
  if (closed.failed())
    return closed;
  camera.shutdown();
  world = application.native_world();
  auto operation = gneiss::engine::error::success;
  if (operation == gneiss::engine::error::success) {
    auto initial = completion.content
                       ? make_scene_document_snapshot(uri_, *completion.content, completion.source)
                       : engine::result<scene_document_snapshot>{engine::error::invalid_state};
    if (!initial)
      operation = initial.error();
    else {
      initial->assets = std::move(completion.bindings);
      operation = editing.document().adopt(application, world, completion.scene, *initial,
                                           completion.objects);
    }
  }
  if (operation == gneiss::engine::error::success) {
    operation = camera.initialize();
  }
  progress_ = completion.progress;
  operation_ = operation;
  active_ = false;
  visible_ = operation != gneiss::engine::error::success;
  changed = true;
  // 激活后的绑定错误无法回滚域，交给宿主报告并退出，避免使用部分绑定状态。
  return operation;
}

} // namespace gneiss::editor
