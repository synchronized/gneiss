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
  if (active_ || reload_) {
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
runtime_scene_loader::activate(::gneiss::host::scene_open_result& completion) {
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
  completion.reload_state = engine::make_ref<scene_reload_state>();
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
  advance_reload();
  if (!active_ || !ready_->load())
    return engine::error::success;
  // 共享批次仍串行提交；只在资源批次可提交时构建运行实例。
  if (!application_.can_load_assets())
    return engine::error::success;
  scene_open_result completion;
  completion.content = request_.snapshot();
  auto loaded = request_.take();
  if (!loaded) {
    result_ = loaded.error().error;
    progress_.message = loaded.error().message;
  } else {
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
engine::error runtime_scene_loader::reload(engine::ref<scene_reload_state> source,
                                           std::string_view uri, bool prefab) {
  if (!application_.is_owner_thread() || !application_.accepts_requests())
    return engine::error::invalid_state;
  if (!source || source->owner != application_.native_world() || !source->synchronized ||
      !source->content || !source->content->description)
    return engine::error::invalid_state;
  auto instance = engine::scene_instance::borrow(*application_.native_world(), source->instance);
  if (!instance)
    return instance.error();
  if (active_ || reload_ || visuals_ || !application_.can_load_assets())
    return engine::error::not_ready;
  reload_task next;
  next.source = std::move(source);
  if (prefab) {
    auto loaded = application_.assets().reload<engine::prefab>(uri);
    if (!loaded)
      return loaded.error();
    next.request = std::move(*loaded);
  } else {
    auto loaded = application_.assets().reload<engine::scene>(uri);
    if (!loaded)
      return loaded.error();
    next.request = std::move(*loaded);
  }
  next.ready = std::make_shared<std::atomic<bool>>(false);
  std::visit(
      [ready = std::weak_ptr{next.ready}](auto& request) {
        engine::detail::asset_request_access::when_ready(request, [ready] {
          if (auto value = ready.lock())
            value->store(true);
        });
      },
      next.request);
  reload_ = std::move(next);
  return engine::error::success;
}
engine::error runtime_scene_loader::apply_source(scene_reload_state& source,
                                                 engine::ref<const engine::scene_content> content) {
  if (!source.synchronized || !content || !content->description ||
      content->description->uuid != source.content->description->uuid)
    return engine::error::invalid_state;
  const auto compose = [](const engine::scene_content& source) {
    std::map<std::string, const engine::prefab_description*, std::less<>> prefabs;
    for (const auto& [uri, dependency] : source.prefabs)
      prefabs.emplace(uri, dependency.description.get());
    return engine::compose_scene(*source.description, prefabs);
  };
  auto previous = compose(*source.content);
  auto next = compose(*content);
  if (!previous)
    return previous.error();
  if (!next)
    return next.error();
  auto rebased =
      engine::rebase_scene_update(*application_.native_world(), source.objects, *previous, *next);
  if (!rebased)
    return rebased.error();
  auto dependencies = engine::scene_dependencies(*next);
  if (!dependencies)
    return dependencies.error();
  auto visual_request = application_.load_assets(*dependencies);
  if (!visual_request)
    return visual_request.error();
  engine::scene_resource_bindings bindings;
  for (const auto& request : *dependencies) {
    auto resource = application_.assets().load(request);
    if (!resource)
      return resource.error();
    bindings.emplace(request.uri, resource->resource());
  }
  source.synchronized = false;
  auto applied = engine::apply_scene(*application_.native_world(), source.instance, source.objects,
                                     *previous, *next, bindings);
  if (!applied)
    return applied.error();
  if (!*applied)
    return engine::error::invalid_state;
  source.content = std::move(content);
  source.synchronized = true;
  visuals_ = std::move(*visual_request);
  return engine::error::success;
}

void runtime_scene_loader::advance_reload() {
  if (!reload_ || reload_->completion || !reload_->ready->load() || !application_.can_load_assets())
    return;
  auto& task = *reload_;
  auto result = std::visit(
      [&](auto& request) -> engine::error {
        // 在领取前捕获本次请求的固定内容，避免读取身份上随后更新的内容。
        auto snapshot = request.snapshot();
        auto loaded = request.take();
        if (!loaded)
          return loaded.error().error;
        auto& source = *task.source;
        if (!source.synchronized || source.owner != application_.native_world())
          return engine::error::invalid_state;
        auto instance = engine::scene_instance::borrow(*source.owner, source.instance);
        if (!instance)
          return instance.error();
        engine::ref<const engine::scene_content> content;
        if constexpr (std::is_same_v<typename std::remove_cvref_t<decltype(request)>::value_type,
                                     engine::scene>) {
          content = snapshot ? snapshot : (*loaded)->content();
        } else {
          auto replacement = engine::make_ref<engine::scene_content>(*source.content);
          auto dependency = replacement->prefabs.find((*loaded)->source().uri);
          if (dependency == replacement->prefabs.end())
            return engine::error::not_found;
          dependency->second = {*loaded, snapshot ? snapshot : (*loaded)->snapshot(),
                                request.source_bytes()};
          content = std::move(replacement);
        }
        return apply_source(source, std::move(content));
      },
      task.request);
  scene_reload_result completion{result, {}};
  if (result.ok()) {
    for (const auto& [address, binding] : task.source->objects)
      completion.objects.push_back({address.object, address.source, binding.entity});
  }
  task.completion = std::move(completion);
}
engine::result<scene_reload_result> runtime_scene_loader::take_reload() {
  if (!reload_)
    return engine::error::invalid_state;
  if (!reload_->completion)
    return engine::error::not_ready;
  auto completion = std::move(*reload_->completion);
  reload_.reset();
  return completion;
}
} // namespace gneiss::host
