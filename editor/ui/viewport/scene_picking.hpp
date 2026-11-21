// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/scene_edit_session.hpp"
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/function/scene/rendering/world_render_binding.hpp>
#include <optional>

namespace gneiss::editor {
/** Scene View 的最新点击及异步选择协调；只在编辑器所属线程使用。
 * 不拥有文档、World 或 Renderer，不修改文档内容与撤销历史。 */
class scene_picking final {
public:
  ~scene_picking() { cancel(); }
  scene_picking() = default;
  scene_picking(const scene_picking&) = delete;
  scene_picking& operator=(const scene_picking&) = delete;
  /** uv 是实际显示图像内的归一化坐标；右/下边界不包含。新点击取代旧点击。 */
  void queue(const scene_edit_session&, rid target, render::target_description, math::vector2 uv);
  /** 在 World 同步后调用；背压保留点击，其余错误返回给编辑器显示，不终止应用。 */
  error update(render::service&, const world_render_binding&, scene_edit_session&,
               const render::view&, render::target_description);
  void cancel();
  bool active() const noexcept { return pending_.has_value(); }

private:
  struct pending {
    scene_document::revision document;
    entity_id selection;
    rid target{};
    render::target_description extent;
    std::uint32_t x{}, y{};
    std::optional<render::view> view;
    render::pick_request request;
    bool submitted{};
  };
  std::optional<pending> pending_;
};
}
