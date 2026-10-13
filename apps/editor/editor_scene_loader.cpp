// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_scene_loader.hpp"

namespace gneiss::editor {

result editor_scene_loader::request(gneiss_application application, std::string_view uri) {
  if (active_) {
    return result::not_ready;
  }
  if (application == GNEISS_NULL_APPLICATION || uri.empty()) {
    return result::invalid_argument;
  }
  uri_ = uri;
  progress_ = {};
  operation_ = result::success;
  active_ = true;
  visible_ = true;
  queued_ = true;
  return result::success;
}

result editor_scene_loader::cancel(gneiss_application application) {
  if (!active_) {
    return result::not_ready;
  }
  if (queued_) {
    queued_ = false;
    active_ = false;
    operation_ = result::invalid_state;
    progress_.phase = application_internal::scene_load_phase::cancelled;
    progress_.cleanup_complete = true;
    return result::success;
  }
  const auto operation =
      from_native(application_internal::cancel_scene_load(application, request_));
  if (operation == result::success) {
    progress_.can_cancel = false;
  }
  return operation;
}

result editor_scene_loader::advance(gneiss_application application, editor_session& session,
                                    gneiss_world& world, editor_camera& camera,
                                    editor_command_history& history, bool& changed) {
  using namespace application_internal;
  changed = false;
  // 即使界面已收起也推进旧域回收，下一次打开不能被退休域永久阻塞。
  scene_load_completion completion;
  bool finished{};
  const auto polled = from_native(poll_scene_load(application, completion, finished));
  if (polled != result::success) {
    return polled;
  }
  if (!active_) {
    return result::success;
  }
  if (queued_) {
    // 先回收旧域再提交，菜单回调本身不做加载工作。
    queued_ = false;
    operation_ = from_native(request_scene_load(application, uri_, 1U, ++revision_, request_));
    active_ = operation_ == result::success;
    if (!active_) {
      progress_.phase = scene_load_phase::failed;
    } else {
      progress_.can_cancel = true;
    }
    return result::success;
  }
  if (finished) {
    progress_ = completion.progress;
    operation_ = from_native(completion.result);
    active_ = progress_.cleanup_pending;
    return result::success;
  }
  bool available{};
  const auto queried = from_native(query_scene_load_progress(application, progress_, available));
  if (queried != result::success) {
    return queried;
  }
  if (!available) {
    return result::success;
  }
  if (progress_.cleanup_complete && progress_.phase == scene_load_phase::cancelled) {
    active_ = false;
    return result::success;
  }
  if (progress_.phase != scene_load_phase::ready) {
    return result::success;
  }
  const auto activated = from_native(activate_scene_load(application, request_, completion));
  if (activated == result::not_ready) {
    return result::success;
  }
  if (activated != result::success) {
    return activated;
  }

  // 激活后旧域仍存活至下一次推进；先释放依赖旧 World 的编辑器状态。
  history.clear();
  camera.shutdown();
  session.close();
  auto operation = from_native(gneiss_application_get_world(application, &world));
  if (operation == result::success) {
    operation = session.adopt(application, world, completion.scene, uri_);
  }
  if (operation == result::success) {
    operation = camera.initialize(world);
  }
  progress_ = completion.progress;
  operation_ = operation;
  active_ = false;
  visible_ = operation != result::success;
  changed = true;
  // 激活后的绑定错误无法回滚域，交给宿主报告并退出，避免使用部分绑定状态。
  return operation;
}

} // namespace gneiss::editor
