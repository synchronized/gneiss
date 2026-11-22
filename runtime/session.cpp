// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "session.hpp"
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/scene/rendering/render_camera.hpp>
namespace gneiss::runtime {
result<void> session::initialize() {
  if (world_)
    return error::invalid_state;
  auto created = create_native();
  if (!created)
    return created.error();
  world_ = std::move(*created);
  auto bound = world_render_binding::create(*world_, application_.render());
  if (!bound)
    return bound.error();
  binding_ = std::move(*bound);
  loader_.emplace(application_, *world_);
  return {};
}
result<void> session::synchronize() {
  world_->flush_changes();
  return binding_->synchronize();
}
result<void> session::append_views(render::frame_request& frame) const {
  if (!frame.surface.width || !frame.surface.height)
    return {};
  auto selected = select_render_camera(*world_, frame.surface.width, frame.surface.height);
  if (!selected)
    return selected.error();
  if (*selected) {
    frame.views.push_back({.scene = binding_->scene(),
                           .view_matrix = (**selected).view,
                           .projection_matrix = (**selected).projection,
                           .camera_position = (**selected).transform.translation});
  } else
    frame.views.push_back({});
  return {};
}
void session::stop_loading() {
  if (loader_)
    loader_->stop();
}
void session::close() {
  stop_loading();
  loader_.reset();
  binding_.reset();
  world_.reset();
}
}
