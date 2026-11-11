// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/editor_scene_loader.hpp"
#include "application/application.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/camera_selection.hpp"
#include "engine/function/scene/instantiation.hpp"
#include "engine/function/scene/world.hpp"

namespace gneiss::editor {

gneiss::engine::error editor_scene_loader::request(runtime::application& application,
                                                   const document_editing& editing,
                                                   std::string_view uri) {
  if (active() || editing.active()) {
    return gneiss::engine::error::not_ready;
  }
  if (!application.accepts_requests() || !application.is_owner_thread() || uri.empty()) {
    return gneiss::engine::error::invalid_argument;
  }
  uri_ = uri;
  operation_ = gneiss::engine::error::success;
  state_ = scene_open_state::loading;
  visible_ = true;
  queued_ = true;
  return gneiss::engine::error::success;
}

gneiss::engine::error editor_scene_loader::cancel(runtime::application& application) {
  if (!active()) {
    return gneiss::engine::error::not_ready;
  }
  if (!application.accepts_requests() || !application.is_owner_thread())
    return engine::error::invalid_state;
  request_.cancel();
  ready_.reset();
  queued_ = false;
  operation_ = engine::error::invalid_state;
  state_ = scene_open_state::cancelled;
  return engine::error::success;
}

gneiss::engine::error editor_scene_loader::advance(runtime::application& application,
                                                   document_editing& editing,
                                                   gneiss::engine::world*& world,
                                                   editor_camera& camera, bool& changed) {
  changed = false;
  if (visuals_ && visuals_.ready()) {
    auto completed = visuals_.take();
    if (completed) {
      for (const auto& item : completed->results) {
        if (item.result.failed())
          (void)application.submit_log({.severity = engine::logging::level::error,
                                        .source = "editor",
                                        .category = "scene_assets",
                                        .message = item.source.uri,
                                        .result = item.result});
      }
    }
    visuals_.reset();
  }
  // 有编辑未结束时不切换文档，避免使编辑绑定失效。
  if (editing.active() || !active())
    return engine::error::success;
  const auto fail = [&](engine::error error) {
    request_.cancel();
    ready_.reset();
    operation_ = error;
    state_ = scene_open_state::failed;
    return engine::error{engine::error::success};
  };
  if (queued_) {
    queued_ = false;
    auto submitted = application.assets().reload<engine::scene>(uri_);
    if (!submitted)
      return fail(submitted.error());
    request_ = std::move(*submitted);
    ready_ = std::make_shared<std::atomic<bool>>(false);
    engine::detail::asset_request_access::when_ready(request_, [ready = std::weak_ptr{ready_}] {
      if (auto value = ready.lock())
        value->store(true);
    });
    return engine::error::success;
  }
  if (!ready_->load() || !application.can_load_assets())
    return engine::error::success;
  auto content = request_.snapshot();
  auto loaded = request_.take();
  if (!loaded)
    return fail(loaded.error().error);
  if (!content)
    content = (*loaded)->content();
  auto initial = make_scene_document_snapshot(uri_, *content, *loaded);
  if (!initial)
    return fail(initial.error());
  std::map<std::string, const engine::prefab_description*, std::less<>> sources;
  for (const auto& [uri, source] : content->prefabs)
    sources.emplace(uri, source.description.get());
  auto composed = engine::compose_scene(*content->description, sources);
  if (!composed)
    return fail(composed.error());
  auto dependencies = engine::scene_dependencies(*composed);
  if (!dependencies)
    return fail(dependencies.error());
  auto visual_request = application.load_assets(*dependencies);
  if (!visual_request)
    return fail(visual_request.error());
  for (const auto& dependency : *dependencies) {
    auto resource = application.assets().load(dependency);
    if (!resource)
      return fail(resource.error());
    initial->assets.emplace(dependency.uri, resource->resource());
  }
  auto next = application.native_world()->create_scene();
  if (!next)
    return fail(next.error());
  auto built = engine::instantiate(*next, *composed, initial->assets);
  if (!built) {
    (void)next->unload();
    return fail(built.error());
  }
  entity_id primary_camera;
  for (std::size_t i = 0; i < composed->objects.size(); ++i) {
    const auto* description =
        composed->objects[i].effective.components.get<engine::camera_description>();
    if (description && description->is_primary)
      primary_camera = built->objects[i].entity;
  }
  if (primary_camera.get()) {
    auto selected = engine::set_active_camera(*application.native_world(), primary_camera);
    if (selected.failed()) {
      (void)next->unload();
      return fail(selected);
    }
  }
  const auto closed = editing.close();
  if (closed.failed()) {
    (void)next->unload();
    return closed;
  }
  camera.shutdown();
  world = application.native_world();
  auto operation =
      editing.document().adopt(application, world, next->id(), *initial, built->objects);
  if (operation.failed())
    (void)next->unload();
  else {
    visuals_ = std::move(*visual_request);
    operation = camera.initialize();
  }
  ready_.reset();
  state_ = operation.ok() ? scene_open_state::opened : scene_open_state::failed;
  operation_ = operation;
  visible_ = operation.failed();
  changed = true;
  // 切换后的绑定错误交给宿主报告并退出，不能继续使用部分绑定状态。
  return operation;
}

} // namespace gneiss::editor
