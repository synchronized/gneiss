// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/editor_scene_loader.hpp"
#include "viewport/scene_viewport.hpp"
namespace gneiss::editor {
/** 一次编辑会话的唯一所有者；Application 必须覆盖整个会话生命期。
 * 构造仅装配；服务就绪后 initialize，退出时 cancel/advance_cleanup，最后 close。
 * 所有操作限宿主线程，析构不推进异步任务。 */
class session final {
public:
  explicit session(app::application& application) : application_(application) {}
  session(const session&) = delete;
  session& operator=(const session&) = delete;
  error initialize();
  error open_scene(std::string_view);
  error create_empty();
  error update(bool& scene_changed);
  error cancel_load();
  void dismiss_load() { loader_.dismiss(); }
  const editor_scene_loader& loading() const noexcept { return loader_; }
  document_editing& editing() noexcept { return editing_; }
  scene_viewport& viewport() noexcept { return viewport_; }
  gneiss::world* world() const noexcept { return world_.get(); }
  /** binding 失败返回给调度器；可恢复拾取错误单独交给 UI。 */
  result<void> synchronize(error& interaction_error);
  result<void> append_views(render::frame_request&) const;
  void cancel();
  void advance_cleanup();
  bool busy() const { return editing_.active() || loader_.active(); }
  /** 在途编辑或目标释放背压返回 not_ready；借用先结束再销毁 World，可重复调用。 */
  error close();

private:
  app::application& application_;
  world_owner world_;
  std::unique_ptr<world_render_binding> binding_;
  document_editing editing_;
  scene_viewport viewport_;
  editor_scene_loader loader_;
};
}
