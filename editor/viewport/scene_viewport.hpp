// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/document_editing.hpp"
#include "ui/viewport/editor_camera.hpp"
#include "ui/viewport/scene_picking.hpp"
#include "ui/viewport/transform_gizmo_view.hpp"
#include <gneiss/engine/function/render/camera.hpp>
namespace gneiss::editor {
/** 单个编辑视口的相机、目标和交互状态；所属线程使用。
 * 不拥有 World、文档或渲染绑定。关闭时须在 Render 服务销毁前调用 close，背压可重试。 */
class scene_viewport final {
public:
  scene_viewport() = default;
  scene_viewport(const scene_viewport&) = delete;
  scene_viewport& operator=(const scene_viewport&) = delete;
  error initialize() { return camera_.initialize(); }
  error close(render::service&);
  /** 像素尺寸由 UI 根据实际布局及 DPI 换算；背压期间保留旧图像。 */
  error resize(render::service&, render::target_description);
  rid target() const noexcept { return target_; }
  render::target_description extent() const noexcept { return extent_; }
  /** 加载器借用相机完成打开场景后的取景；不转移所有权。 */
  editor_camera& camera() noexcept { return camera_; }
  const editor_camera& camera() const noexcept { return camera_; }
  error update_camera(const editor_camera_input&, bool focus, document_editing&, world&);
  transform_gizmo_operation operation() const noexcept { return operation_; }
  void set_operation(transform_gizmo_operation value) noexcept { operation_ = value; }
  bool dragging() const noexcept { return drag_.is_active(); }
  error finish_interaction(document_editing&, bool mouse_down);
  void reset_interaction();
  bool draw_gizmo(document_editing&, error&, const ImVec2&, const ImVec2&);
  void select_at(scene_edit_session&, math::vector2 uv, bool gizmo_active);
  void cancel_pick() { picking_.cancel(); }
  /** 调用前先成功同步对应 World 的渲染绑定；仅推进拾取，不更新 World。 */
  error synchronize(render::service&, const world_render_binding&, document_editing&, world&);
  result<std::optional<render::view>> view(world&, rid scene, render::target_description) const;

private:
  editor_camera camera_;
  rid target_{}, retired_target_{};
  render::target_description extent_{};
  transform_gizmo_operation operation_{transform_gizmo_operation::translate};
  transform_gizmo_drag drag_;
  bool wait_release_{};
  scene_picking picking_;
};
}
