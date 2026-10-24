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
#include "engine/resource/vfs/source_revision_file_system.hpp"

#include <algorithm>
#include <stdexcept>
#include <stop_token>

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
    std::unique_ptr<engine::resource::source_revision_file_system::verification> verification;
    bool verification_complete{};
  };
  std::shared_ptr<cpu_result> cpu{std::make_shared<cpu_result>()};
  engine::core::tasks::task_handle task;
  std::stop_source source_stop;
  std::shared_ptr<engine::resource::source_revision_file_system> sources;
  engine::resource::virtual_file_system snapshot;
  bool verifying{};
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
  if (pending_) {
    pending_->source_stop.request_stop();
  }
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
  next->sources = std::make_shared<engine::resource::source_revision_file_system>(
      files_, [token = next->source_stop.get_token()] { return token.stop_requested(); });
  const auto mounted = next->snapshot.mount("asset://", next->sources);
  if (mounted != gneiss::engine::core::result::success) {
    return mounted;
  }
  next->result.progress = {
      .request = ++sequence_, .session = session, .revision = revision, .can_cancel = true};
  const auto accepted = executor_->submit(
      {.name = "scene.prepare", .scope = scope_, .notification = notification_},
      [cpu = next->cpu, files = next->snapshot,
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
  pending_->source_stop.request_stop();
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
engine::core::tasks::submit_result scene_service::submit_verification() {
  GNEISS_PROFILE_SCOPE("scene.verify.submit");
  const engine::core::diagnostics::loop_span body(
      engine::core::diagnostics::loop_stage::scene_verify_submit_body);
  auto function = engine::core::diagnostics::measure(
      engine::core::diagnostics::loop_stage::task_callback_create, [&] {
        return engine::core::tasks::task_executor::task_function{
            [cpu = pending_->cpu,
             sources = pending_->sources](const engine::core::tasks::task_context& context) {
              GNEISS_PROFILE_SCOPE("scene.verify.step");
              if (!cpu->verification) {
                cpu->result = sources->begin_verification(cpu->verification);
              }
              if (cpu->result == gneiss::engine::core::result::success) {
                // 每个任务只复验一段；由宿主消费回执后继续提交，不嵌套驱动或增加线程。
                // 协作任务不可被驱动预算抢占，缩小单块校验；后台保留吞吐批次。
                const auto byte_budget =
                    std::size_t{context.allows_blocking_wait() ? 16U : 4U} * 1024U * 1024U;
                cpu->result = cpu->verification->advance(
                    byte_budget, [&] { return context.stop_requested(); },
                    cpu->verification_complete,
                    std::chrono::milliseconds(context.allows_blocking_wait() ? 4 : 0));
              }
              if (context.stop_requested()) {
                return engine::core::tasks::task_outcome{
                    .state = engine::core::tasks::task_state::cancelled};
              }
              return engine::core::tasks::task_outcome{
                  .state = cpu->result == gneiss::engine::core::result::success
                               ? engine::core::tasks::task_state::succeeded
                               : engine::core::tasks::task_state::failed,
              };
            }};
      });
  return engine::core::diagnostics::submit_observed(
      *executor_, {.name = "scene.verify", .scope = scope_, .notification = notification_},
      std::move(function), pending_->task);
}
void scene_service::advance_verification(load_task& value) {
  const engine::core::diagnostics::loop_span span(
      engine::core::diagnostics::loop_stage::scene_verify);
  auto& progress = value.result.progress;
  if (value.task.id != 0U) {
    std::vector<engine::core::tasks::task_completion> completions;
    engine::core::diagnostics::measure(engine::core::diagnostics::loop_stage::task_collect,
                                       [&] { return executor_->poll(scope_, completions); });
    if (completions.empty()) {
      return;
    }
    value.task = {};
    value.result.verify_ms += completions.front().execution_ms;
    value.result.verify_maximum_step_ms =
        std::max(value.result.verify_maximum_step_ms, completions.front().execution_ms);
  }
  if (value.cancelled) {
    finish(gneiss::engine::core::result::invalid_state, scene_load_phase::cancelled);
    return;
  }
  if (value.cpu->result != gneiss::engine::core::result::success) {
    finish(value.cpu->result, scene_load_phase::failed, "Scene/Prefab 结构来源在准备期间变化");
    return;
  }
  if (value.cpu->verification) {
    progress.completed = value.cpu->verification->completed_sources();
    const auto timings = value.cpu->verification->timings();
    value.result.verify_maximum_open_ms = timings.maximum_open_ms;
    value.result.verify_maximum_read_ms = timings.maximum_read_ms;
    value.result.verify_maximum_hash_ms = timings.maximum_hash_ms;
  }
  if (!value.cpu->verification_complete) {
    const auto accepted = engine::core::diagnostics::measure(
        engine::core::diagnostics::loop_stage::task_submit, [&] { return submit_verification(); });
    if (accepted != engine::core::tasks::submit_result::success &&
        accepted != engine::core::tasks::submit_result::full) {
      finish(gneiss::engine::core::result::invalid_state, scene_load_phase::failed);
    }
    return;
  }
  value.verifying = false;
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
  if (value.verifying) {
    advance_verification(value);
    return;
  }
  if (value.cancelled) {
    finish(gneiss::engine::core::result::invalid_state, scene_load_phase::cancelled);
    return;
  }
  if (progress.phase == scene_load_phase::assets) {
    if (value.cursor == value.requested.size()) {
      const auto accepted =
          engine::core::diagnostics::measure(engine::core::diagnostics::loop_stage::task_submit,
                                             [&] { return submit_verification(); });
      if (accepted == engine::core::tasks::submit_result::full) {
        return;
      }
      if (accepted != engine::core::tasks::submit_result::success) {
        finish(gneiss::engine::core::result::invalid_state, scene_load_phase::failed);
        return;
      }
      value.verifying = true;
      progress.phase = scene_load_phase::verifying;
      progress.completed = 0U;
      progress.total = value.sources->source_count();
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
