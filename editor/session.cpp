// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "session.hpp"
#include <gneiss/engine/function/application/application.hpp>
namespace gneiss::editor {
error session::initialize() {
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
  auto opened = editing_.create_empty(application_, world_.get());
  if (opened.failed())
    return opened;
  return viewport_.initialize();
}
error session::open_scene(std::string_view uri) {
  return loader_.request(application_, editing_, uri);
}
error session::create_empty() {
  auto status = editing_.create_empty(application_, world_.get());
  if (status.ok())
    viewport_.reset_interaction();
  return status;
}
error session::update(bool& scene_changed) {
  auto status = loader_.advance(application_, editing_, *world_, viewport_.camera(), scene_changed);
  if (scene_changed)
    viewport_.reset_interaction();
  return status;
}
error session::cancel_load() { return loader_.cancel(application_); }
result<void> session::synchronize(error& interaction_error) {
  world_->flush_changes();
  auto synced = binding_->synchronize();
  if (!synced)
    return synced;
  const auto picked = viewport_.synchronize(application_.render(), *binding_, editing_, *world_);
  if (picked.failed())
    interaction_error = picked;
  return {};
}
result<void> session::append_views(render::frame_request& frame) const {
  const auto extent = viewport_.target()
                          ? viewport_.extent()
                          : render::target_description{frame.surface.width, frame.surface.height};
  if (extent.width && extent.height) {
    auto selected = viewport_.view(*world_, binding_->scene(), extent);
    if (!selected)
      return selected.error();
    if (*selected)
      frame.views.push_back(**selected);
  }
  if (viewport_.target() && frame.surface.width && frame.surface.height)
    frame.views.push_back({});
  return {};
}
void session::cancel() {
  viewport_.cancel_pick();
  editing_.cancel(application_);
  if (loader_.active())
    (void)loader_.cancel(application_);
}
void session::advance_cleanup() { editing_.advance_cleanup(application_); }
error session::close() {
  if (busy())
    return error::not_ready;
  auto closed = viewport_.close(application_.render());
  if (closed.failed())
    return closed;
  editing_.prefabs().dismiss();
  closed = editing_.close();
  if (closed.failed())
    return closed;
  // 丢弃加载回执与视觉依赖批次，防止会话重新初始化后消费旧结果。
  loader_ = {};
  binding_.reset();
  world_.reset();
  return error::success;
}
}
