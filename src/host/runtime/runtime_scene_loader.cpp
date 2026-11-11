// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_scene_loader.hpp"
#include "application/application.hpp"
#include "engine/core/error.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/scene/camera_selection.hpp"
#include "engine/function/scene/instantiation.hpp"

namespace gneiss::host {
namespace {
const char* phase_message(ipc_scene_phase phase) {
  switch (phase) {
  case ipc_scene_phase::preparing:
    return "正在准备场景描述";
  case ipc_scene_phase::assets:
    return "正在建立场景资源引用";
  case ipc_scene_phase::instantiating:
    return "正在构造候选场景";
  case ipc_scene_phase::ready:
    return "等待安全点激活";
  case ipc_scene_phase::applied:
    return "场景已激活";
  case ipc_scene_phase::failed:
    return "场景加载失败，保留原场景";
  case ipc_scene_phase::cancelled:
    return "场景加载已取消";
  }
  return "未知场景加载状态";
}

}
gneiss::engine::error runtime_scene_loader::request(ipc_scene_request source,
                                                    std::uint32_t request_id) {
  if (active_) {
    return gneiss::engine::error::not_ready;
  }
  if (source.session == progress_.source.session && source.revision <= progress_.source.revision) {
    return gneiss::engine::error::invalid_state;
  }
  auto result = application_.assets().reload<engine::scene>(source.uri);
  if (!result)
    return result.error();
  request_ = std::move(*result);
  ready_ = std::make_shared<std::atomic<bool>>(false);
  engine::detail::asset_request_access::when_ready(request_, [ready = std::weak_ptr{ready_}] {
    if (auto value = ready.lock())
      value->store(true);
  });
  request_id_ = request_id;
  result_ = gneiss::engine::error::success;
  progress_ = {.source = std::move(source), .can_cancel = true, .message = "正在准备场景描述"};
  active_ = true;
  return gneiss::engine::error::success;
}
gneiss::engine::error runtime_scene_loader::cancel(const ipc_scene_request& source) {
  if (!active_ || source.session != progress_.source.session ||
      source.revision != progress_.source.revision || source.uri != progress_.source.uri) {
    return gneiss::engine::error::not_ready;
  }
  request_.cancel();
  ready_.reset();
  active_ = false;
  result_ = engine::error::invalid_state;
  progress_.phase = ipc_scene_phase::cancelled;
  progress_.can_cancel = false;
  progress_.message = "场景加载已取消";
  progress_.budget = ipc_scene_budget{.cleanup_complete = true};
  return engine::error::success;
}
gneiss::engine::error
runtime_scene_loader::activate(::gneiss::runtime::scene_load_completion& completion) {
  if (before_activate) {
    const auto stopped = before_activate();
    if (stopped != gneiss::engine::error::success) {
      return stopped;
    }
  }
  std::map<std::string, const engine::prefab_description*, std::less<>> sources;
  for (const auto& [uri, source] : completion.content->prefabs)
    sources.emplace(uri, source.description.get());
  auto composed = engine::compose_scene(*completion.content->description, sources);
  if (!composed)
    return composed.error();
  auto dependencies = engine::scene_dependencies(*composed);
  if (!dependencies)
    return dependencies.error();
  auto visual_request = application_.load_assets(*dependencies);
  if (!visual_request)
    return visual_request.error();
  engine::scene_resource_bindings bindings;
  for (const auto& dependency : *dependencies) {
    auto loaded = application_.assets().load(dependency);
    if (!loaded)
      return loaded.error();
    bindings.emplace(dependency.uri, loaded->resource());
  }
  auto next = application_.native_world()->create_scene();
  if (!next)
    return next.error();
  auto built = engine::instantiate(*next, *composed, bindings);
  if (!built) {
    (void)next->unload();
    return built.error();
  }
  entity_id camera;
  for (std::size_t i = 0; i < composed->objects.size(); ++i) {
    auto* description = composed->objects[i].effective.components.get<engine::camera_description>();
    if (description && description->is_primary)
      camera = built->objects[i].entity;
  }
  if (camera.get()) {
    auto selected = engine::set_active_camera(*application_.native_world(), camera);
    if (selected.failed()) {
      (void)next->unload();
      return selected;
    }
  }
  completion.scene = next->id();
  completion.objects = std::move(built->objects);
  completion.bindings = std::move(bindings);
  completion.reload_state = engine::make_ref<runtime::scene_reload_state>();
  auto& baseline = *completion.reload_state;
  baseline.owner = application_.native_world();
  baseline.instance = completion.scene;
  baseline.content = completion.content;
  for (const auto& binding : completion.objects)
    baseline.objects.emplace(engine::object_address{binding.object, binding.source},
                             engine::scene_object_association{binding.entity});
  if (current_.is_valid()) {
    const auto unloaded = current_.unload();
    if (unloaded.failed()) {
      (void)next->unload();
      return unloaded;
    }
  }
  completion.result = engine::error::success;
  completion.progress.phase = runtime::scene_load_phase::applied;
  completion.progress.completed = completion.objects.size();
  completion.progress.total = completion.objects.size();
  current_ = *next;
  visuals_ = std::move(*visual_request);
  // 激活后的任意模块副作用不在场景事务的自动回滚承诺内。
  return after_activate ? after_activate(completion) : gneiss::engine::error::success;
}
gneiss::engine::error runtime_scene_loader::advance() {
  if (visuals_ && visuals_.ready()) {
    auto completed = visuals_.take();
    if (completed) {
      for (const auto& item : completed->results) {
        if (item.result.failed())
          (void)application_.submit_log({.severity = engine::level::error,
                                         .source = "runtime",
                                         .category = "scene_assets",
                                         .message = item.source.uri,
                                         .result = item.result});
      }
    }
    visuals_.reset();
  }
  if (!active_ || !ready_->load())
    return engine::error::success;
  // 共享批次仍串行提交；只在资源批次可提交时构建运行实例。
  if (!application_.can_activate_scene())
    return engine::error::success;
  runtime::scene_load_completion completion;
  completion.content = request_.snapshot();
  auto loaded = request_.take();
  if (!loaded) {
    result_ = loaded.error().error;
    progress_.message = loaded.error().message;
  } else {
    completion.source = *loaded;
    if (!completion.content)
      completion.content = (*loaded)->content();
    result_ = activate(completion);
    progress_.completed = static_cast<std::uint32_t>(completion.objects.size());
    progress_.total = progress_.completed;
    progress_.message =
        phase_message(result_.ok() ? ipc_scene_phase::applied : ipc_scene_phase::failed);
  }
  request_.cancel();
  ready_.reset();
  active_ = false;
  progress_.phase = result_.ok() ? ipc_scene_phase::applied : ipc_scene_phase::failed;
  progress_.can_cancel = false;
  const auto usage = application_.render().memory_usage();
  progress_.budget =
      ipc_scene_budget{.application_logical_bytes = usage.logical_bytes,
                       .application_cpu_data_bytes = usage.cpu_data_bytes,
                       .available_bytes = application_.render().available_memory_bytes(),
                       .cleanup_complete = true};
  if (progress_.message.empty() || result_.ok())
    progress_.message = phase_message(progress_.phase);
  // 已替换场景后的宿主回调失败不能伪装成保留旧场景的加载失败。
  if (result_.failed() && current_.is_valid() && current_.id() == completion.scene)
    return result_;
  return engine::error::success;
}
} // namespace gneiss::host
