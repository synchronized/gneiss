// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/loading/scene_service.hpp"
#include "engine/core/diagnostics/loop_timing.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include "engine/function/scene/scene_instantiation.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/loading/resource_load_group.hpp"
#include "runtime/loading/scene_domain.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <map>
#include <type_traits>
#include <utility>

namespace gneiss::runtime {

gneiss::engine::error scene_service::initialize(gneiss::engine::asset_manager& resources) {
  if (active_ || stopping_) {
    return gneiss::engine::error::invalid_state;
  }
  auto next = std::make_unique<scene_domain>();
  const auto result = next->initialize();
  if (result != gneiss::engine::error::success) {
    return result;
  }
  resources_ = &resources;
  active_ = std::move(next);
  return gneiss::engine::error::success;
}

gneiss::engine::error scene_service::configure(gneiss::engine::render_service& renderer) {
  if (loading_ || !active_ || stopping_) {
    return gneiss::engine::error::invalid_state;
  }
  renderer_ = &renderer;
  loading_ = std::make_unique<resource_load_group>(*resources_);
  return gneiss::engine::error::success;
}

void scene_service::advance() {
  check_owner();
  if (stopping_) {
    advance_stop();
    return;
  }
  if (retired_) {
    const auto start = std::chrono::steady_clock::now();
    const gneiss::engine::loop_span span(gneiss::engine::loop_stage::retirement);
    retired_.reset();
    resources_->collect_unused();
    retirement_.last_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    ++retirement_.retired_domains;
  }
  advance_visuals();
  advance_load();
  advance_reload();
}

void scene_service::visual_load_state::advance(resource_load_group& group) {
  if (completion) {
    return;
  }
  if (pending) {
    group.advance();
    gneiss::runtime::asset_load_completion finished;
    if (!group.take(finished)) {
      return;
    }
    pending = false;
    completion = std::move(finished);
    return;
  }
  if (cursor == queue.size()) {
    queue.clear();
    cursor = 0U;
    return;
  }
  std::uint64_t request{};
  const auto submitted =
      group.submit_assets(std::span(queue).subspan(cursor, 1U), session, revision, request, false);
  if (submitted == gneiss::engine::error::not_ready) {
    return;
  }
  if (submitted == gneiss::engine::error::success) {
    pending = true;
  } else {
    gneiss::runtime::asset_load_completion finished;
    finished.result = submitted;
    finished.results.push_back({.source = queue[cursor], .result = submitted});
    completion = std::move(finished);
  }
  ++cursor;
}

bool scene_service::visual_load_state::take(gneiss::runtime::asset_load_completion& output) {
  if (!completion) {
    return false;
  }
  output = std::move(*completion);
  completion.reset();
  return true;
}

void scene_service::visual_load_state::begin(std::vector<engine::render_resource_source> sources,
                                             std::uint64_t next_session,
                                             std::uint64_t next_revision) {
  queue = std::move(sources);
  cursor = 0;
  session = next_session;
  revision = next_revision;
}
void scene_service::visual_load_state::cancel(resource_load_group& group) {
  queue.clear();
  cursor = 0;
  // 取消回执仍由 advance/take 消费，不能提前清除 pending。
  if (pending)
    (void)group.cancel();
}
void scene_service::visual_load_state::stop(resource_load_group* group) {
  queue.clear();
  completion.reset();
  if (group)
    group->request_stop();
}
void scene_service::visual_load_state::reset() noexcept { *this = {}; }
void scene_service::advance_visuals() {
  if (loading_)
    visuals_.advance(*loading_);
}
bool scene_service::take_visual_completion(asset_load_completion& output) {
  return visuals_.take(output);
}

void scene_service::shutdown() {
  stopping_ = true;
  visuals_.reset();
  reload_.reset();
  source_.reset();
  stop_load();
  loading_.reset();
  retired_.reset();
  active_.reset();
  if (resources_ != nullptr) {
    resources_->collect_unused();
  }
  resources_ = nullptr;
}

bool scene_service::can_start_load() const noexcept {
  return !stopping_ && !reload_ && !retired_ && loading_ && (!loading_->busy() || visuals_.pending);
}
bool scene_service::loading() const noexcept {
  return pending_ != nullptr || completed_.has_value();
}
scene_retirement_statistics scene_service::retirement() const noexcept {
  auto value = retirement_;
  value.pending = retired_ != nullptr;
  return value;
}
gneiss::engine::world* scene_service::native_world() const noexcept {
  return active_ ? active_->native_world() : nullptr;
}

gneiss::engine::scene_instances* scene_service::scenes() noexcept {
  return active_ ? active_->scenes() : nullptr;
}
bool scene_service::can_activate() const noexcept {
  return !stopping_ && !reload_ && !retired_ && loading_ && !loading_->busy();
}
engine::error scene_service::request_reload(scene_instance_id instance, std::string_view uri,
                                            bool prefab) {
  check_owner();
  if (stopping_ || !active_ || !loading_ || !resources_)
    return engine::error::invalid_state;
  if (reload_ || loading() || visual_loading() || loading_->busy() || retired_)
    return engine::error::not_ready;
  auto valid_value = scenes()->get_node_count(instance);
  if (!valid_value)
    return valid_value.error();

  if (!source_ || source_->instance != instance || !source_->synchronized)
    return engine::error::invalid_state;
  auto next = std::make_unique<reload_task>();
  next->instance = instance;
  if (prefab) {
    auto loaded = resources_->reload<engine::prefab>(uri);
    if (!loaded)
      return loaded.error();
    next->descriptions = std::move(*loaded);
  } else {
    auto loaded = resources_->reload<engine::scene>(uri);
    if (!loaded)
      return loaded.error();
    next->descriptions = std::move(*loaded);
  }
  reload_ = std::move(next);
  return engine::error::success;
}

engine::result<std::vector<engine::render_resource_source>>
scene_service::apply_source(engine::ref<const engine::scene_content> content) {
  if (!source_ || !source_->synchronized || !content || !content->description ||
      content->description->uuid != source_->content->description->uuid)
    return engine::error::invalid_state;
  engine::scene_preparation_inputs inputs;
  inputs.scene = content->description;
  for (const auto& [uri, dependency] : content->prefabs) {
    inputs.prefabs.emplace(uri, dependency.description);
    inputs.prefab_assets.emplace(uri, dependency.resource);
  }
  engine::scene_instantiation_data prepared;
  engine::scene_diagnostic diagnostic;
  auto status = engine::prepare_loaded_scene(inputs, {}, prepared, diagnostic, {});
  if (status.failed())
    return status;
  const auto compose = [](const engine::scene_content& source) {
    std::map<std::string, const engine::prefab_description*, std::less<>> prefabs;
    for (const auto& [uri, dependency] : source.prefabs)
      prefabs.emplace(uri, dependency.description.get());
    return engine::compose_scene(*source.description, prefabs);
  };
  auto previous = compose(*source_->content);
  auto next = compose(*content);
  if (!previous)
    return previous.error();
  if (!next)
    return next.error();
  // 来源未修改的变换保留游戏运行值；来源修改才覆盖该值。
  const auto same_transform = [](const auto& a, const auto& b) {
    return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
  };
  for (auto& object : previous->objects) {
    const auto binding = source_->objects.find(object.address);
    if (binding == source_->objects.end())
      return engine::error::invalid_state;
    auto runtime = native_world()->object(binding->second.entity);
    if (!runtime)
      return runtime.error();
    auto transform = runtime->local_transform();
    if (!transform)
      return transform.error();
    auto replacement =
        std::ranges::find(next->objects, object.address, &engine::composed_object::address);
    if (replacement != next->objects.end() &&
        same_transform(object.effective.transform, replacement->effective.transform))
      replacement->effective.transform = *transform;
    object.effective.transform = *transform;
    // 用实际运行状态比较目标，恢复来源指定的名称、组件和父关系。
    auto identity = native_world()->node_identity(binding->second.node);
    auto components = engine::capture_components(*native_world(), binding->second.node);
    if (!identity)
      return identity.error();
    if (!components)
      return components.error();
    object.effective.name = identity->name;
    object.effective.components = std::move(*components);
    engine::object_address parent_address;
    scene_node_id parent_node;
    auto parent_status = native_world()->node_get_parent(binding->second.node, parent_node);
    if (parent_status.failed())
      return parent_status;
    if (parent_node) {
      auto parent = std::ranges::find_if(
          source_->objects, [&](const auto& entry) { return entry.second.node == parent_node; });
      if (parent == source_->objects.end())
        return engine::error::invalid_state;
      parent_address = parent->first;
    }
    object.parent = parent_address;
  }
  engine::scene_resource_bindings bindings;
  for (const auto& request : prepared.assets) {
    auto resource = resources_->load(request);
    if (!resource)
      return resource.error();
    bindings.emplace(request.uri, resource->resource());
  }
  source_->synchronized = false;
  auto applied = engine::apply_scene(*native_world(), source_->instance, source_->objects,
                                     *previous, *next, bindings);
  if (!applied)
    return applied.error();
  if (!*applied)
    return engine::error::invalid_state;
  source_->content = std::move(content);
  source_->synchronized = true;
  return std::move(prepared.assets);
}

void scene_service::advance_reload() {
  if (!reload_ || reload_->result != engine::error::not_ready)
    return;
  auto& value = *reload_;
  auto reloaded = std::visit(
      [&](auto& request)
          -> engine::result<std::vector<engine::render_resource_source>, engine::scene_diagnostic> {
        auto loaded = request.take();
        if (!loaded) {
          auto failure = std::move(loaded).error();
          return engine::scene_diagnostic{failure.error, failure.byte_offset,
                                          std::move(failure.path), std::move(failure.message)};
        }
        if (source_ && source_->instance == value.instance) {
          engine::ref<const engine::scene_content> content;
          if constexpr (std::is_same_v<std::remove_cvref_t<decltype(**loaded)>, engine::scene>) {
            content = (*loaded)->content();
          } else {
            auto replacement = engine::make_ref<engine::scene_content>(*source_->content);
            auto dependency = replacement->prefabs.find((*loaded)->source().uri);
            if (dependency != replacement->prefabs.end())
              dependency->second = {*loaded, (*loaded)->snapshot(), request.source_bytes()};
            content = std::move(replacement);
          }
          auto applied = apply_source(std::move(content));
          if (!applied)
            return engine::scene_diagnostic{applied.error(), 0, {}, {}};
          return std::move(*applied);
        }
        return engine::scene_diagnostic{engine::error::invalid_state, 0, {}, {}};
      },
      value.descriptions);
  if (!reloaded) {
    if (reloaded.error().error != engine::error::not_ready)
      value.result = reloaded.error().error;
    return;
  }
  if (source_ && source_->instance == value.instance) {
    for (const auto& [address, binding] : source_->objects)
      value.objects.push_back({address.object, address.source, binding.node, binding.entity});
  }
  value.result = engine::error::success;
  visuals_.begin(std::move(*reloaded), 1, 1);
}

engine::result<std::optional<scene_reload_completion>> scene_service::poll_reload() {
  check_owner();
  if (!reload_)
    return engine::error::invalid_state;
  if (reload_->result == engine::error::not_ready)
    return std::optional<scene_reload_completion>{};
  scene_reload_completion completed{reload_->result, std::move(reload_->objects)};
  reload_.reset();
  return std::optional{completed};
}

} // namespace gneiss::runtime
