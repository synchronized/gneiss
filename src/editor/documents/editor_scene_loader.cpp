// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/editor_scene_loader.hpp"
#include "engine/core/result.hpp"
#include "runtime/application.hpp"

namespace gneiss::editor {

gneiss::engine::core::result editor_scene_loader::request(runtime::application& application,
                                                          const document_editing& editing,
                                                          std::string_view uri) {
  if (active_ || editing.active()) {
    return gneiss::engine::core::result::not_ready;
  }
  if (!application.accepts_requests() || !application.is_owner_thread() || uri.empty()) {
    return gneiss::engine::core::result::invalid_argument;
  }
  uri_ = uri;
  progress_ = {};
  operation_ = gneiss::engine::core::result::success;
  active_ = true;
  visible_ = true;
  queued_ = true;
  return gneiss::engine::core::result::success;
}

gneiss::engine::core::result editor_scene_loader::cancel(runtime::application& application) {
  if (!active_) {
    return gneiss::engine::core::result::not_ready;
  }
  if (queued_) {
    queued_ = false;
    active_ = false;
    operation_ = gneiss::engine::core::result::invalid_state;
    progress_.phase = runtime::scene_load_phase::cancelled;
    progress_.cleanup_complete = true;
    return gneiss::engine::core::result::success;
  }
  const auto operation = application.cancel_scene(request_);
  if (operation == gneiss::engine::core::result::success) {
    progress_.can_cancel = false;
  }
  return operation;
}

gneiss::engine::core::result editor_scene_loader::advance(runtime::application& application,
                                                          document_editing& editing,
                                                          gneiss_world& world,
                                                          editor_camera& camera, bool& changed) {
  using namespace runtime;
  changed = false;
  // 防御非 UI 调用方：有编辑未结束时不推进场景域，避免切换或退休旧 World。
  if (editing.active())
    return gneiss::engine::core::result::success;
  // 即使界面已收起也推进旧域回收，下一次打开不能被退休域永久阻塞。
  scene_load_completion completion;
  bool finished{};
  const auto polled = application.poll_scene(completion, finished);
  if (polled != gneiss::engine::core::result::success) {
    return polled;
  }
  if (!active_) {
    return gneiss::engine::core::result::success;
  }
  if (queued_) {
    // 先回收旧域再提交，菜单回调本身不做加载工作。
    queued_ = false;
    operation_ = application.request_scene(uri_, 1U, ++revision_, request_);
    active_ = operation_ == gneiss::engine::core::result::success;
    if (!active_) {
      progress_.phase = scene_load_phase::failed;
    } else {
      progress_.can_cancel = true;
    }
    return gneiss::engine::core::result::success;
  }
  if (finished) {
    progress_ = completion.progress;
    operation_ = completion.result;
    active_ = progress_.cleanup_pending;
    return gneiss::engine::core::result::success;
  }
  bool available{};
  available = application.scene_progress(progress_);
  const auto queried = gneiss::engine::core::result::success;
  if (queried != gneiss::engine::core::result::success) {
    return queried;
  }
  if (!available) {
    return gneiss::engine::core::result::success;
  }
  if (progress_.cleanup_complete && progress_.phase == scene_load_phase::cancelled) {
    active_ = false;
    return gneiss::engine::core::result::success;
  }
  if (progress_.phase != scene_load_phase::ready) {
    return gneiss::engine::core::result::success;
  }
  const auto activated = application.activate_scene(request_, completion);
  if (activated == gneiss::engine::core::result::not_ready) {
    return gneiss::engine::core::result::success;
  }
  if (activated != gneiss::engine::core::result::success) {
    return activated;
  }

  // 激活后旧域仍存活至下一次推进；先释放依赖旧 World 的编辑器状态。
  const auto closed = editing.close();
  if (closed.failed())
    return closed;
  camera.shutdown();
  world = application.world();
  auto operation = gneiss::engine::core::result::success;
  if (operation == gneiss::engine::core::result::success) {
    operation = editing.document().adopt(application, world, completion.scene, uri_);
  }
  if (operation == gneiss::engine::core::result::success) {
    operation = camera.initialize();
  }
  progress_ = completion.progress;
  operation_ = operation;
  active_ = false;
  visible_ = operation != gneiss::engine::core::result::success;
  changed = true;
  // 激活后的绑定错误无法回滚域，交给宿主报告并退出，避免使用部分绑定状态。
  return operation;
}

} // namespace gneiss::editor
