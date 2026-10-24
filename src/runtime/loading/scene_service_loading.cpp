// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/diagnostics/profiling.hpp"
#include "engine/core/diagnostics/task_submission.hpp"
#include "engine/core/result.hpp"
#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"
#include "engine/resource/asset_manager.hpp"
#include "runtime/loading/scene_service.hpp"

#include "engine/resource/loaders/texture_container.hpp"
#include "engine/resource/vfs/asset_uri.hpp"

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
  struct cpu_result {
    engine::scene::prepared_scene_description description;
    engine::resource::scene_diagnostic diagnostic;
    gneiss::engine::core::result result{gneiss::engine::core::result::internal};
  };
  std::shared_ptr<cpu_result> cpu{std::make_shared<cpu_result>()};
  engine::core::tasks::task_handle task;
  scene_load_completion result;
  std::unique_ptr<scene_domain> candidate;
  engine::scene::scene_instances::asset_bindings bindings;
  std::vector<engine::render::render_asset_reload> requested;
  std::size_t cursor{};
  bool preparing{true};
  bool cancelled{};
};

scene_service::scene_service() = default;
scene_service::~scene_service() noexcept { shutdown(); }

void scene_service::stop_load() noexcept {
  if (executor_) {
    executor_->cancel_scope(scope_);
    (void)executor_->close_scope(scope_);
  }
  pending_.reset();
  completed_.reset();
  executor_ = nullptr;
  scope_ = {};
  resources_ = nullptr;
  notification_.reset();
  files_ = {};
}
void scene_service::check_owner() const {
  if (std::this_thread::get_id() != owner_) {
    throw std::logic_error("场景加载服务须在所属线程操作");
  }
}
gneiss::engine::core::result scene_service::request_scene(std::string_view uri,
                                                          std::uint64_t session,
                                                          std::uint64_t revision,
                                                          std::uint64_t& request) {
  check_owner();
  request = 0U;
  if (!executor_ || !can_start_load() || loading()) {
    return gneiss::engine::core::result::not_ready;
  }
  if (engine::resource::validate_uri(uri) != gneiss::engine::core::result::success ||
      session == 0U || revision == 0U) {
    return gneiss::engine::core::result::invalid_argument;
  }
  auto next = std::make_unique<load_task>();
  next->result.progress = {
      .request = ++sequence_, .session = session, .revision = revision, .can_cancel = true};
  const auto accepted = executor_->submit(
      {.name = "scene.prepare", .scope = scope_, .notification = notification_},
      [cpu = next->cpu, files = files_,
       source = std::string(uri)](const engine::core::tasks::task_context& context) {
        cpu->result = engine::scene::prepare_scene_description(
            files, source, cpu->description, cpu->diagnostic,
            [&] { return context.stop_requested(); });
        if (context.stop_requested()) {
          return engine::core::tasks::task_outcome{.state =
                                                       engine::core::tasks::task_state::cancelled};
        }
        return engine::core::tasks::task_outcome{
            .state = cpu->result == gneiss::engine::core::result::success
                         ? engine::core::tasks::task_state::succeeded
                         : engine::core::tasks::task_state::failed,
        };
      },
      next->task);
  if (accepted != engine::core::tasks::submit_result::success) {
    return accepted == engine::core::tasks::submit_result::full
               ? gneiss::engine::core::result::not_ready
               : gneiss::engine::core::result::invalid_state;
  }
  request = next->result.progress.request;
  pending_ = std::move(next);
  visual_queue_.clear();
  visual_cursor_ = 0U;
  if (visual_pending_) {
    (void)textures_->cancel_unpublished();
  }
  return gneiss::engine::core::result::success;
}
void scene_service::sample_budget(scene_load_progress& value) const {
  const auto usage = resources_->memory_usage();
  value.application_logical_bytes = usage.logical_bytes;
  value.application_cpu_data_bytes = usage.cpu_data_bytes;
  value.available_bytes = resources_->available_memory_bytes();
  value.upload_reserved_bytes = 0U;
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
gneiss::engine::core::result scene_service::cancel_scene(std::uint64_t request) {
  check_owner();
  if (!pending_ || pending_->result.progress.request != request) {
    return gneiss::engine::core::result::not_ready;
  }
  pending_->cancelled = true;
  pending_->result.progress.can_cancel = false;
  (void)executor_->cancel(pending_->task);
  return gneiss::engine::core::result::success;
}
void scene_service::finish(gneiss::engine::core::result result, scene_load_phase phase,
                           std::string message) {
  pending_->result.result = result;
  pending_->result.progress.phase = phase;
  pending_->result.progress.can_cancel = false;
  pending_->result.message = std::move(message);
  sample_budget(pending_->result.progress);
  auto completion = std::move(pending_->result);
  const auto cleanup_started = clock_type::now();
  pending_.reset();
  render_assets_->release_unused();
  asset_owner_->collect_unused();
  // 候选分项保留结束前的诊断值，Application/上传账本必须在请求销毁后采样。
  sample_budget(completion.progress);
  completion.progress.gpu_in_flight = false;
  completion.progress.cleanup_complete = true;
  completion.progress.cleanup_pending = false;
  completion.cleanup_ms = elapsed(cleanup_started);
  completed_ = std::move(completion);
}
gneiss::engine::core::result scene_service::poll_scene(scene_load_completion& result, bool& ready) {
  check_owner();
  ready = false;
  if (!executor_) {
    return gneiss::engine::core::result::not_ready;
  }
  advance();
  if (completed_) {
    result = std::move(*completed_);
    completed_.reset();
    ready = true;
  }
  return gneiss::engine::core::result::success;
}

gneiss::engine::core::result scene_service::activate(std::uint64_t request,
                                                     scene_load_completion& result) {
  check_owner();
  if (!executor_ || !can_activate() || !pending_ || pending_->cancelled ||
      pending_->result.progress.request != request ||
      pending_->result.progress.phase != scene_load_phase::ready) {
    return gneiss::engine::core::result::not_ready;
  }
  const auto start = clock_type::now();
  sample_budget(pending_->result.progress);
  result = std::move(pending_->result);
  result.result = gneiss::engine::core::result::success;
  result.progress.phase = scene_load_phase::applied;
  result.progress.can_cancel = false;
  retired_ = std::move(active_);
  active_ = std::move(pending_->candidate);
  visual_queue_ = std::move(pending_->requested);
  visual_cursor_ = 0U;
  visual_session_ = result.progress.session;
  visual_revision_ = result.progress.revision;
  pending_.reset();
  result.activation_ms = elapsed(start);
  return gneiss::engine::core::result::success;
}
void scene_service::advance_load() {
  GNEISS_PROFILE_SCOPE("scene.advance");
  const engine::core::diagnostics::loop_span span(
      engine::core::diagnostics::loop_stage::scene_advance);
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
    finish(gneiss::engine::core::result::out_of_memory, scene_load_phase::failed);
  } catch (...) {
    finish(gneiss::engine::core::result::internal, scene_load_phase::failed);
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
  progress.total = value.cpu->description.instance_nodes;
  gneiss_scene_instance instance{};
  const auto built =
      engine::core::diagnostics::measure(engine::core::diagnostics::loop_stage::scene_build, [&] {
        return value.candidate->scenes()->instantiate_prepared(std::move(value.cpu->description),
                                                               value.bindings, instance);
      });
  if (built != gneiss::engine::core::result::success) {
    finish(built, scene_load_phase::failed);
    return;
  }
  value.result.scene = instance;
  progress.completed = progress.total;
  progress.phase = scene_load_phase::ready;
}
void scene_service::advance_load_impl() {
  auto& value = *pending_;
  auto& progress = value.result.progress;
  if (value.preparing) {
    std::vector<engine::core::tasks::task_completion> completions;
    engine::core::diagnostics::measure(engine::core::diagnostics::loop_stage::task_collect,
                                       [&] { return executor_->poll(scope_, completions); });
    if (completions.empty()) {
      return;
    }
    value.preparing = false;
    value.result.prepare_ms = completions.front().execution_ms;
    if (value.cancelled) {
      finish(gneiss::engine::core::result::invalid_state, scene_load_phase::cancelled);
      return;
    }
    if (value.cpu->result != gneiss::engine::core::result::success) {
      finish(value.cpu->result, scene_load_phase::failed, value.cpu->diagnostic.message);
      return;
    }
    value.candidate = std::make_unique<scene_domain>();
    const auto initialized = value.candidate->initialize(*asset_owner_, *render_assets_);
    if (initialized != gneiss::engine::core::result::success) {
      finish(initialized, scene_load_phase::failed);
      return;
    }
    value.requested = std::move(value.cpu->description.assets);
    progress.phase = scene_load_phase::assets;
    progress.total = value.requested.size();
    return;
  }
  if (value.cancelled) {
    finish(gneiss::engine::core::result::invalid_state, scene_load_phase::cancelled);
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
      engine::render::render_asset_lease lease;
      const auto reserved = render_assets_->reserve_asset(source, lease);
      if (reserved != gneiss::engine::core::result::success) {
        finish(reserved, scene_load_phase::failed, "场景资源引用无效");
        return;
      }
      value.bindings.emplace(source.uri, std::move(lease));
    }
    progress.completed = value.cursor;
    return;
  }
}

} // namespace gneiss::runtime
