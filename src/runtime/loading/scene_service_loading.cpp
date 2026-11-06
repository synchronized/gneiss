// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/diagnostics/profiling.hpp"
#include "engine/core/diagnostics/task_submission.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "runtime/loading/resource_load_group.hpp"
#include "runtime/loading/scene_domain.hpp"
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
  engine::scene_instantiation_data description;
  engine::asset_request<engine::scene> description_request;
  scene_load_completion result;
  std::unique_ptr<scene_domain> candidate;
  gneiss::engine::scene_instances::resource_bindings bindings;
  std::vector<gneiss::engine::render_resource_source> requested;
  std::size_t cursor{};
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
  notification_.reset();
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
  auto prepared = resources_->load<engine::scene>(uri, true);
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
  const auto start = clock_type::now();
  sample_budget(pending_->result.progress);
  auto result = std::move(pending_->result);
  result.result = gneiss::engine::error::success;
  result.progress.phase = scene_load_phase::applied;
  result.progress.can_cancel = false;
  retired_ = std::move(active_);
  active_ = std::move(pending_->candidate);
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
  const auto previous_phase = pending_->result.progress.phase;
  const auto previous_completed = pending_->result.progress.completed;
  const auto start = clock_type::now();
  try {
    advance_load_impl();
  } catch (const std::bad_alloc&) {
    finish(gneiss::engine::error::out_of_memory, scene_load_phase::failed);
  } catch (...) {
    finish(gneiss::engine::error::internal, scene_load_phase::failed);
  }
  if (notification_ && (!pending_ || pending_->result.progress.phase != previous_phase ||
                        pending_->result.progress.completed != previous_completed)) {
    notification_->notify();
  }
  auto* result = pending_ ? &pending_->result : completed_ ? &*completed_ : nullptr;
  if (result != nullptr) {
    result->maximum_advance_ms = std::max(result->maximum_advance_ms, elapsed(start));
  }
}
void scene_service::instantiate_loaded(load_task& value) {
  auto& progress = value.result.progress;
  progress.phase = scene_load_phase::instantiating;
  progress.completed = 0U;
  progress.total = value.description.instance_nodes;
  const auto built = gneiss::engine::measure(gneiss::engine::loop_stage::scene_build, [&] {
    return value.candidate->scenes()->instantiate_prepared(std::move(value.description),
                                                           value.bindings);
  });
  if (!built) {
    finish(built.error(), scene_load_phase::failed);
    return;
  }
  value.result.scene = *built;
  progress.completed = progress.total;
  progress.phase = scene_load_phase::ready;
}
void scene_service::advance_load_impl() {
  auto& value = *pending_;
  auto& progress = value.result.progress;
  if (progress.phase == scene_load_phase::preparing) {
    if (value.cancelled) {
      finish(engine::error::invalid_state, scene_load_phase::cancelled);
      return;
    }
    auto prepared = value.description_request.take();
    if (!prepared) {
      if (prepared.error().error != engine::error::not_ready)
        finish(prepared.error().error, scene_load_phase::failed, prepared.error().message);
      return;
    }
    const auto prepare_started = clock_type::now();
    auto data =
        engine::make_instantiation_data(*prepared, value.description_request.source_bytes());
    if (!data) {
      finish(data.error().error, scene_load_phase::failed, data.error().message);
      return;
    }
    value.description = std::move(*data);
    value.result.prepare_ms = elapsed(prepare_started);
    value.candidate = std::make_unique<scene_domain>();
    const auto initialized = value.candidate->initialize(*resources_);
    if (initialized != gneiss::engine::error::success) {
      finish(initialized, scene_load_phase::failed);
      return;
    }
    value.requested = std::move(value.description.assets);
    progress.phase = scene_load_phase::assets;
    progress.total = value.requested.size();
    return;
  }
  if (value.cancelled) {
    finish(gneiss::engine::error::invalid_state, scene_load_phase::cancelled);
    return;
  }
  if (progress.phase == scene_load_phase::assets) {
    if (value.cursor == value.requested.size()) {
      instantiate_loaded(value);
      return;
    }
    // 只预留稳定引用；视觉内容不参与结构激活条件。
    const auto end = std::min(value.cursor + 64U, value.requested.size());
    for (; value.cursor < end; ++value.cursor) {
      const auto& source = value.requested[value.cursor];
      auto reserved = resources_->reserve_resource(source);
      if (!reserved) {
        finish(reserved.error(), scene_load_phase::failed, "场景资源引用无效");
        return;
      }
      value.bindings.emplace(source.uri, std::move(*reserved));
    }
    progress.completed = value.cursor;
    return;
  }
}

} // namespace gneiss::runtime
