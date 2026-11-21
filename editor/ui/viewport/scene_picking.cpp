// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "ui/viewport/scene_picking.hpp"
#include <algorithm>
#include <cmath>
namespace gneiss::editor {
void scene_picking::cancel() {
  if (pending_)
    pending_->request.cancel();
  pending_.reset();
}
void scene_picking::queue(const scene_edit_session& session, rid target,
                          render::target_description extent, math::vector2 uv) {
  cancel();
  if (!session.is_open() || !extent.width || !extent.height || !std::isfinite(uv.x) ||
      !std::isfinite(uv.y) || uv.x < 0 || uv.y < 0 || uv.x >= 1 || uv.y >= 1)
    return;
  pending_.emplace();
  auto& click = *pending_;
  click.document = session.current_version().stamp();
  click.selection = session.selection();
  click.target = target;
  click.extent = extent;
  // 用 double 乘法避免接近右/下边缘时 float 舍入到范围之外。
  click.x = std::min(extent.width - 1U,
                     static_cast<std::uint32_t>(static_cast<double>(uv.x) * extent.width));
  click.y = std::min(extent.height - 1U,
                     static_cast<std::uint32_t>(static_cast<double>(uv.y) * extent.height));
}
error scene_picking::update(render::service& renderer, const world_render_binding& binding,
                            scene_edit_session& session, const render::view& view,
                            render::target_description extent) {
  if (!pending_)
    return error::success;
  auto& click = *pending_;
  const auto same_view = [&] {
    if (!click.view)
      return true;
    const auto& previous = *click.view;
    return previous.scene == view.scene && previous.target == view.target &&
           previous.view_matrix == view.view_matrix &&
           previous.projection_matrix == view.projection_matrix &&
           previous.camera_position == view.camera_position && previous.layers == view.layers;
  };
  if (!session.is_open() || session.current_version().stamp() != click.document ||
      session.selection() != click.selection || view.scene != binding.scene() ||
      click.target != view.target || click.extent.width != extent.width ||
      click.extent.height != extent.height || !same_view()) {
    cancel();
    return error::success;
  }
  if (!click.submitted) {
    click.view = view;
    auto requested = renderer.request_pick(view, extent, click.x, click.y);
    if (!requested) {
      if (requested.error() == error::not_ready)
        return error::success;
      const auto status = requested.error();
      cancel();
      return status;
    }
    click.request = std::move(*requested);
    click.submitted = true;
  }
  if (!click.request.ready())
    return error::success;
  auto hit = click.request.take();
  cancel();
  if (!hit)
    return hit.error() == error::cancelled ? error::success : hit.error();
  if (hit->scene != binding.scene())
    return error::success;
  if (!hit->instance)
    return session.select({});
  auto object = binding.resolve(*hit);
  // 非零命中已过期时保留选择；不能把对象删除误解释成背景点击。
  return object ? session.select(object.entity()) : error::success;
}
}
