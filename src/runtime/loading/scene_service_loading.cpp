// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/diagnostics/profiling.hpp"
#include "engine/core/diagnostics/task_submission.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/scene/composition.hpp"
#include "engine/function/scene/instantiation.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/resource/scene.hpp"
#include "runtime/loading/resource_load_group.hpp"
#include "runtime/loading/scene_service.hpp"

#include "engine/resource/asset_uri.hpp"

#include <algorithm>
#include <stdexcept>

namespace gneiss::runtime {
namespace {
using clock_type = std::chrono::steady_clock;
double elapsed(clock_type::time_point start) {
  return std::chrono::duration<double, std::milli>(clock_type::now() - start).count();
}
} // namespace

struct scene_service::load_task {
  engine::asset_request<engine::scene> description_request;
  scene_load_completion result;
  engine::world_owner candidate;
  entity_id default_camera;
  std::vector<gneiss::engine::render_resource_source> requested;
  bool cancelled{};
};

scene_service::scene_service() = default;
scene_service::~scene_service() noexcept { shutdown(); }

void scene_service::request_stop() {
  check_owner();
  if (stopping_)
    return;
  stopping_ = true;
  reload_.reset();
  visuals_.stop(loading_.get());
  if (pending_)
    pending_->cancelled = true;
}
bool scene_service::stopped() const noexcept {
  return stopping_ && !loading_ && !active_ && !retired_;
}
void scene_service::advance_stop() {
  if (stopped())
    return;
  if (loading_)
    loading_->advance();
  advance_load();
  if (loading_ && !loading_->stopped())
    return;
  // 场景已放弃资产请求；共享资产任务由资产管理器负责收尾。
  shutdown();
}

void scene_service::stop_load() noexcept {
  pending_.reset();
  completed_.reset();
  renderer_ = nullptr;
}
void scene_service::check_owner() const {
  if (std::this_thread::get_id() != owner_) {
    throw std::logic_error("场景加载服务须在所属线程操作");
  }
}
engine::result<std::uint64_t>
scene_service::request_scene(std::string_view uri, std::uint64_t session, std::uint64_t revision) {
  check_owner();
  if (!can_start_load() || loading()) {
    return gneiss::engine::error::not_ready;
  }
  if (gneiss::engine::validate_uri(uri) != gneiss::engine::error::success || session == 0U ||
      revision == 0U) {
    return gneiss::engine::error::invalid_argument;
  }
  auto next = std::make_unique<load_task>();
  next->result.progress = {
      .request = ++sequence_, .session = session, .revision = revision, .can_cancel = true};
  auto prepared = resources_->reload<engine::scene>(uri);
  if (!prepared)
    return prepared.error();
  next->description_request = std::move(*prepared);
  const auto request = next->result.progress.request;
  pending_ = std::move(next);
  visuals_.cancel(*loading_);
  return request;
}
void scene_service::sample_budget(scene_load_progress& value) const {
  const auto usage = renderer_->memory_usage();
  value.application_logical_bytes = usage.logical_bytes;
  value.application_cpu_data_bytes = usage.cpu_data_bytes;
  value.available_bytes = renderer_->available_memory_bytes();
}
bool scene_service::scene_progress(scene_load_progress& value) const {
  check_owner();
  if (pending_) {
    value = pending_->result.progress;
    sample_budget(value);
    return true;
  }
  if (completed_) {
    value = completed_->progress;
    return true;
  }
  return false;
}
gneiss::engine::error scene_service::cancel_scene(std::uint64_t request) {
  check_owner();
  if (!pending_ || pending_->result.progress.request != request) {
    return gneiss::engine::error::not_ready;
  }
  pending_->cancelled = true;
  pending_->result.progress.can_cancel = false;
  return gneiss::engine::error::success;
}
void scene_service::finish(gneiss::engine::error result, scene_load_phase phase,
                           std::string message) {
  pending_->result.result = result;
  pending_->result.progress.phase = phase;
  pending_->result.progress.can_cancel = false;
  pending_->result.message = std::move(message);
  sample_budget(pending_->result.progress);
  auto completion = std::move(pending_->result);
  completion.objects.clear();
  completion.bindings.clear();
  completion.content.reset();
  completion.source.reset();
  const auto cleanup_started = clock_type::now();
  pending_.reset();
  resources_->collect_unused();
  // 候选分项保留结束前的诊断值，Application/上传账本必须在请求销毁后采样。
  sample_budget(completion.progress);
  completion.progress.cleanup_complete = true;
  completion.progress.cleanup_pending = false;
  completion.cleanup_ms = elapsed(cleanup_started);
  completed_ = std::move(completion);
}
engine::result<std::optional<scene_load_completion>> scene_service::poll_scene() {
  check_owner();
  if (!loading_)
    return engine::error::not_ready;
  advance();
  auto completed = std::move(completed_);
  completed_.reset();
  return completed;
}

engine::result<scene_load_completion> scene_service::activate(std::uint64_t request) {
  check_owner();
  if (!can_activate() || !pending_ || pending_->cancelled ||
      pending_->result.progress.request != request ||
      pending_->result.progress.phase != scene_load_phase::ready) {
    return gneiss::engine::error::not_ready;
  }
  // 候选装配没有激活副作用；应用在切换安全点选择默认相机。
  if (pending_->default_camera.get()) {
    auto selected = pending_->candidate->set_active_camera(pending_->default_camera);
    if (selected.failed())
      return selected;
  }
  auto source = std::make_unique<source_state>();
  source->instance = pending_->result.scene;
  source->content = pending_->result.content;
  for (const auto& binding : pending_->result.objects)
    source->objects.emplace(engine::object_address{binding.object, binding.source},
                            engine::scene_object_association{binding.node, binding.entity});
  const auto start = clock_type::now();
  sample_budget(pending_->result.progress);
  auto result = std::move(pending_->result);
  result.result = gneiss::engine::error::success;
  result.progress.phase = scene_load_phase::applied;
  result.progress.can_cancel = false;
  retired_ = std::move(active_);
  active_ = std::move(pending_->candidate);
  source_ = std::move(source);
  visuals_.begin(std::move(pending_->requested), result.progress.session, result.progress.revision);
  pending_.reset();
  result.activation_ms = elapsed(start);
  return result;
}
void scene_service::advance_load() {
  GNEISS_PROFILE_SCOPE("scene.advance");
  const gneiss::engine::loop_span span(gneiss::engine::loop_stage::scene_advance);
  check_owner();
  if (!pending_) {
    return;
  }
  const auto start = clock_type::now();
  {
    advance_load_impl();
  }
  auto* result = pending_ ? &pending_->result : completed_ ? &*completed_ : nullptr;
  if (result != nullptr) {
    result->maximum_advance_ms = std::max(result->maximum_advance_ms, elapsed(start));
  }
}
void scene_service::advance_load_impl() {
  auto& value = *pending_;
  auto& progress = value.result.progress;
  if (value.cancelled) {
    finish(engine::error::invalid_state, scene_load_phase::cancelled);
    return;
  }
  if (progress.phase == scene_load_phase::preparing) {
    auto prepared = value.description_request.take();
    if (!prepared) {
      if (prepared.error().error != engine::error::not_ready)
        finish(prepared.error().error, scene_load_phase::failed, prepared.error().message);
      return;
    }
    auto created_world = engine::create_native();
    if (!created_world) {
      finish(created_world.error(), scene_load_phase::failed);
      return;
    }
    value.candidate = std::move(*created_world);
    auto content = (*prepared)->content();
    progress.phase = scene_load_phase::instantiating;
    auto built = engine::measure(
        engine::loop_stage::scene_build,
        [&]() -> engine::result<engine::scene_instantiation_result, engine::scene_diagnostic> {
          std::map<std::string, const engine::prefab_description*, std::less<>> sources;
          for (const auto& [uri, source] : content->prefabs)
            sources.emplace(uri, source.description.get());
          auto composed = engine::compose_scene(*content->description, sources);
          if (!composed)
            return engine::scene_diagnostic{composed.error(), 0, {}, {}};
          auto dependencies = engine::scene_dependencies(*composed);
          if (!dependencies)
            return engine::scene_diagnostic{dependencies.error(), 0, {}, {}};
          engine::scene_resource_bindings bindings;
          for (const auto& request : *dependencies) {
            auto loaded = resources_->load(request);
            if (!loaded)
              return engine::scene_diagnostic{loaded.error(), 0, request.uri,
                                              "无法提交场景视觉加载"};
            bindings.emplace(request.uri, loaded->resource());
          }
          auto& world = *value.candidate;
          auto destination = world.create_scene();
          if (!destination)
            return engine::scene_diagnostic{destination.error(), 0, {}, {}};
          auto result = engine::instantiate(*destination, *composed, bindings);
          if (!result) {
            (void)destination->unload();
            return engine::scene_diagnostic{result.error(), 0, {}, {}};
          }
          // 多个默认提示按组合顺序选择最后一个；没有提示时保留无活动相机。
          for (std::size_t i = 0; i < composed->objects.size(); ++i) {
            const auto* camera =
                composed->objects[i].effective.components.get<engine::camera_description>();
            if (camera && camera->is_primary)
              value.default_camera = result->objects[i].entity;
          }
          result->assets = std::move(*dependencies);
          result->bindings = std::move(bindings);
          return std::move(*result);
        });
    if (!built) {
      finish(built.error().error, scene_load_phase::failed, built.error().message);
      return;
    }
    value.result.source = *prepared;
    value.result.content = std::move(content);
    value.result.objects = std::move(built->objects);
    value.result.bindings = std::move(built->bindings);
    value.result.scene = built->instance;
    value.requested = std::move(built->assets);
    progress.completed = progress.total = built->node_count;
    progress.phase = scene_load_phase::ready;
  }
}

} // namespace gneiss::runtime
