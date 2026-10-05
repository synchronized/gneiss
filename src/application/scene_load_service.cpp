// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/scene_load_service.hpp"

#include "asset/asset_uri.h"
#include "asset/source_revision_file_system.h"
#include "asset/texture_container.h"

#include <algorithm>
#include <stdexcept>
#include <stop_token>

namespace gneiss::application_internal {
namespace {
using clock_type = std::chrono::steady_clock;
double elapsed(clock_type::time_point start) {
  return std::chrono::duration<double, std::milli>(clock_type::now() - start).count();
}
struct resource_usage {
  std::size_t logical{};
  std::size_t cpu{};
  std::size_t texture{};
};
resource_usage resource_bytes(const render_internal::render_resource_service& resources,
                              const render_internal::render_asset_lease& lease) {
  using render_internal::render_asset_type;
  if (lease.type() == render_asset_type::mesh) {
    const auto* mesh = resources.get_mesh(lease.get());
    return {.logical = mesh->data_bytes(), .cpu = mesh->data_bytes(), .texture = 0U};
  }
  if (lease.type() == render_asset_type::texture) {
    const auto* texture = resources.get_texture(lease.get());
    auto size = texture->manifest.size() + texture->payload.size();
    for (const auto& level : texture->levels) {
      size += level.pixels.size();
    }
    const auto payload = texture->payload_source
                             ? static_cast<std::size_t>(texture->payload_source->size())
                             : size - texture->manifest.size();
    const auto cpu =
        size + (texture->payload_source ? texture->payload_source->metadata_bytes() : 0U);
    return {.logical = texture->manifest.size() + payload, .cpu = cpu, .texture = payload};
  }
  return {
      .logical = sizeof(render_internal::material_resource),
      .cpu = sizeof(render_internal::material_resource),
      .texture = 0U,
  };
}
} // namespace

struct scene_load_service::pending {
  struct cpu_result {
    scene_internal::prepared_scene_description description;
    scene_internal::scene_diagnostic diagnostic;
    gneiss_result result{GNEISS_ERROR_INTERNAL};
  };
  std::shared_ptr<cpu_result> cpu{std::make_shared<cpu_result>()};
  tasks::task_handle task;
  std::stop_source source_stop;
  std::shared_ptr<asset_internal::source_revision_file_system> sources;
  asset_internal::virtual_file_system snapshot;
  bool verifying{};
  scene_load_completion result;
  std::unique_ptr<application_scene_state> candidate;
  std::unique_ptr<asset_internal::texture_load_service> assets;
  std::unique_ptr<scene_internal::scene_load_builder> builder;
  std::map<std::uint64_t, resource_usage> resident;
  std::vector<render_internal::render_asset_reload> requested;
  std::size_t cursor{};
  std::size_t batch_count{};
  bool preparing{true};
  bool batch_pending{};
  bool cancelled{};
};

scene_load_service::scene_load_service(tasks::task_executor& executor,
                                       asset_internal::virtual_file_system files,
                                       render_internal::render_resource_service& resources,
                                       asset_internal::texture_upload_backend backend)
    : executor_(executor), scope_(executor.make_scope()), files_(std::move(files)),
      resources_(resources), backend_(std::move(backend)) {
  if (scope_.id == 0U) {
    throw std::invalid_argument("无法创建场景任务作用域");
  }
}
scene_load_service::~scene_load_service() {
  if (pending_) {
    pending_->source_stop.request_stop();
  }
  executor_.cancel_scope(scope_);
  (void)executor_.close_scope(scope_);
  // 资产服务先等待/回收已接受的 GPU 命令，再销毁候选域。
  pending_.reset();
}
void scene_load_service::check_owner() const {
  if (std::this_thread::get_id() != owner_) {
    throw std::logic_error("场景加载服务须在所属线程操作");
  }
}
gneiss_result scene_load_service::submit(std::string_view uri, std::uint64_t session,
                                         std::uint64_t revision, std::uint64_t& request) {
  check_owner();
  request = 0U;
  if (busy()) {
    return GNEISS_ERROR_NOT_READY;
  }
  if (asset_internal::validate_uri(uri) != GNEISS_SUCCESS || session == 0U || revision == 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  auto next = std::make_unique<pending>();
  next->sources = std::make_shared<asset_internal::source_revision_file_system>(
      files_, [token = next->source_stop.get_token()] { return token.stop_requested(); });
  const auto mounted = next->snapshot.mount("asset://", next->sources);
  if (mounted != GNEISS_SUCCESS) {
    return mounted;
  }
  next->result.progress = {
      .request = ++sequence_, .session = session, .revision = revision, .can_cancel = true};
  const auto accepted = executor_.submit(
      {.name = "scene.prepare", .scope = scope_},
      [cpu = next->cpu, files = next->snapshot,
       source = std::string(uri)](const tasks::task_context& context) {
        cpu->result = scene_internal::prepare_scene_description(
            files, source, cpu->description, cpu->diagnostic,
            [&] { return context.stop_requested(); });
        return tasks::task_outcome{.state = context.stop_requested() ? tasks::task_state::cancelled
                                            : cpu->result == GNEISS_SUCCESS
                                                ? tasks::task_state::succeeded
                                                : tasks::task_state::failed};
      },
      next->task);
  if (accepted != tasks::submit_result::success) {
    return accepted == tasks::submit_result::full ? GNEISS_ERROR_NOT_READY
                                                  : GNEISS_ERROR_INVALID_STATE;
  }
  request = next->result.progress.request;
  pending_ = std::move(next);
  return GNEISS_SUCCESS;
}
void scene_load_service::sample_budget(scene_load_progress& value) const {
  const auto usage = resources_.memory_usage();
  value.application_logical_bytes = usage.logical_bytes;
  value.application_cpu_data_bytes = usage.cpu_data_bytes;
  value.available_bytes = resources_.available_memory_bytes();
  value.upload_reserved_bytes = 0U;
  asset_internal::asset_load_progress child;
  if (pending_ && pending_->assets && pending_->assets->progress(child)) {
    value.upload_reserved_bytes = child.upload_reserved_bytes;
    value.peak_upload_bytes = std::max(value.peak_upload_bytes, child.upload_reserved_bytes);
  }
}
bool scene_load_service::progress(scene_load_progress& value) const {
  check_owner();
  if (pending_) {
    value = pending_->result.progress;
    sample_budget(value);
    asset_internal::asset_load_progress child;
    value.gpu_in_flight = pending_->assets && pending_->assets->progress(child) &&
                          child.state == asset_internal::texture_load_state::uploading;
    return true;
  }
  if (completed_) {
    value = completed_->progress;
    return true;
  }
  return false;
}
bool scene_load_service::cancel(std::uint64_t request) {
  check_owner();
  if (!pending_ || pending_->result.progress.request != request) {
    return false;
  }
  pending_->cancelled = true;
  pending_->source_stop.request_stop();
  pending_->result.progress.can_cancel = false;
  (void)executor_.cancel(pending_->task);
  if (pending_->assets) {
    (void)pending_->assets->cancel();
  }
  return true;
}
void scene_load_service::finish(gneiss_result result, scene_load_phase phase, std::string message) {
  pending_->result.result = result;
  pending_->result.progress.phase = phase;
  pending_->result.progress.can_cancel = false;
  pending_->result.message = std::move(message);
  sample_budget(pending_->result.progress);
  completed_ = std::move(pending_->result);
  pending_.reset();
}
bool scene_load_service::take(scene_load_completion& result) {
  check_owner();
  if (!completed_) {
    return false;
  }
  result = std::move(*completed_);
  completed_.reset();
  return true;
}
std::unique_ptr<application_scene_state>
scene_load_service::take_candidate(std::uint64_t request, scene_load_completion& result,
                                   std::unique_ptr<asset_internal::texture_load_service>& assets) {
  check_owner();
  if (!pending_ || pending_->cancelled || pending_->result.progress.request != request ||
      pending_->result.progress.phase != scene_load_phase::ready) {
    return {};
  }
  sample_budget(pending_->result.progress);
  result = std::move(pending_->result);
  result.scene = pending_->builder->instance();
  result.result = GNEISS_SUCCESS;
  result.progress.phase = scene_load_phase::applied;
  result.progress.can_cancel = false;
  auto candidate = std::move(pending_->candidate);
  assets = std::move(pending_->assets);
  pending_.reset();
  return candidate;
}
void scene_load_service::advance() {
  check_owner();
  if (!pending_) {
    return;
  }
  const auto start = clock_type::now();
  try {
    advance_impl();
  } catch (const std::bad_alloc&) {
    finish(GNEISS_ERROR_OUT_OF_MEMORY, scene_load_phase::failed);
  } catch (...) {
    finish(GNEISS_ERROR_INTERNAL, scene_load_phase::failed);
  }
  auto* result = pending_ ? &pending_->result : completed_ ? &*completed_ : nullptr;
  if (result != nullptr) {
    result->maximum_advance_ms = std::max(result->maximum_advance_ms, elapsed(start));
  }
}
void scene_load_service::advance_impl() {
  auto& value = *pending_;
  auto& progress = value.result.progress;
  if (value.preparing) {
    std::vector<tasks::task_completion> completions;
    executor_.poll(scope_, completions);
    if (completions.empty()) {
      return;
    }
    value.preparing = false;
    value.result.prepare_ms = completions.front().execution_ms;
    if (value.cancelled) {
      finish(GNEISS_ERROR_INVALID_STATE, scene_load_phase::cancelled);
      return;
    }
    if (value.cpu->result != GNEISS_SUCCESS) {
      finish(value.cpu->result, scene_load_phase::failed, value.cpu->diagnostic.message);
      return;
    }
    value.candidate = std::make_unique<application_scene_state>(files_, resources_);
    const auto initialized = value.candidate->initialize();
    if (initialized != GNEISS_SUCCESS) {
      finish(initialized, scene_load_phase::failed);
      return;
    }
    value.assets = std::make_unique<asset_internal::texture_load_service>(
        executor_, value.snapshot, value.candidate->assets, backend_);
    value.requested = std::move(value.cpu->description.assets);
    progress.phase = scene_load_phase::assets;
    progress.total = value.requested.size();
    return;
  }
  if (value.verifying) {
    std::vector<tasks::task_completion> completions;
    executor_.poll(scope_, completions);
    if (completions.empty()) {
      return;
    }
    value.verifying = false;
    value.result.verify_ms = completions.front().execution_ms;
    if (value.cancelled) {
      finish(GNEISS_ERROR_INVALID_STATE, scene_load_phase::cancelled);
      return;
    }
    if (value.cpu->result != GNEISS_SUCCESS) {
      finish(value.cpu->result, scene_load_phase::failed, "场景依赖源在跨批次加载期间变化");
      return;
    }
    progress.phase = scene_load_phase::instantiating;
    progress.completed = 0U;
    progress.total = value.cpu->description.instance_nodes;
    value.builder = std::make_unique<scene_internal::scene_load_builder>(
        *value.candidate->scenes, std::move(value.cpu->description));
    // 激活后的热重载必须回到宿主原始 VFS，不能继承一次性加载会话的内容固定规则。
    value.assets = std::make_unique<asset_internal::texture_load_service>(
        executor_, files_, value.candidate->assets, backend_);
    return;
  }
  if (value.batch_pending) {
    value.assets->advance();
    asset_internal::texture_load_completion completion;
    if (!value.assets->take(completion)) {
      return;
    }
    value.batch_pending = false;
    value.result.asset_prepare_ms += completion.prepare_ms;
    value.result.upload_ms += completion.upload_ms;
    progress.peak_upload_bytes = std::max(progress.peak_upload_bytes, completion.peak_upload_bytes);
    if (value.cancelled) {
      finish(GNEISS_ERROR_INVALID_STATE, scene_load_phase::cancelled);
      return;
    }
    if (completion.result != GNEISS_SUCCESS) {
      finish(completion.result, scene_load_phase::failed, completion.message);
      return;
    }
    for (const auto& lease : completion.assets) {
      auto& previous = value.resident[lease.get()];
      progress.resident_bytes -= previous.logical;
      progress.cpu_data_bytes -= previous.cpu;
      progress.texture_payload_bytes -= previous.texture;
      previous = resource_bytes(resources_, lease);
      progress.resident_bytes += previous.logical;
      progress.cpu_data_bytes += previous.cpu;
      progress.texture_payload_bytes += previous.texture;
    }
    if (progress.resident_bytes > maximum_resident_bytes ||
        progress.cpu_data_bytes > maximum_resident_bytes) {
      finish(GNEISS_ERROR_OUT_OF_MEMORY, scene_load_phase::failed,
             "场景候选预算不足：逻辑容量 " + std::to_string(progress.resident_bytes) +
                 " 字节，CPU 数据 " + std::to_string(progress.cpu_data_bytes) + " 字节，各项上限 " +
                 std::to_string(maximum_resident_bytes) + " 字节");
      return;
    }
    value.cursor += value.batch_count;
    progress.completed = value.cursor;
    return;
  }
  if (value.cancelled) {
    finish(GNEISS_ERROR_INVALID_STATE, scene_load_phase::cancelled);
    return;
  }
  if (progress.phase == scene_load_phase::assets) {
    if (value.cursor == value.requested.size()) {
      const auto accepted = executor_.submit(
          {.name = "scene.verify", .scope = scope_},
          [cpu = value.cpu, sources = value.sources](const tasks::task_context& context) {
            cpu->result = sources->verify([&] { return context.stop_requested(); });
            return tasks::task_outcome{
                .state = context.stop_requested()        ? tasks::task_state::cancelled
                         : cpu->result == GNEISS_SUCCESS ? tasks::task_state::succeeded
                                                         : tasks::task_state::failed};
          },
          value.task);
      if (accepted == tasks::submit_result::full) {
        return;
      }
      if (accepted != tasks::submit_result::success) {
        finish(GNEISS_ERROR_INVALID_STATE, scene_load_phase::failed);
        return;
      }
      value.verifying = true;
      progress.phase = scene_load_phase::verifying;
      progress.completed = 0U;
      progress.total = value.sources->source_count();
      return;
    }
    // Material 会展开纹理闭包，单独准备；Mesh 每批最多 16 项，不提高既有字节上限。
    value.batch_count = 1U;
    while (value.batch_count < 16U && value.cursor + value.batch_count < value.requested.size() &&
           value.requested[value.cursor].type == render_internal::render_asset_type::mesh &&
           value.requested[value.cursor + value.batch_count].type ==
               render_internal::render_asset_type::mesh) {
      ++value.batch_count;
    }
    std::uint64_t child{};
    const auto local_remaining =
        maximum_resident_bytes - std::max(progress.resident_bytes, progress.cpu_data_bytes);
    const auto remaining = static_cast<std::size_t>(
        std::min<std::uint64_t>(local_remaining, resources_.available_memory_bytes()));
    if (remaining == 0U) {
      finish(GNEISS_ERROR_OUT_OF_MEMORY, scene_load_phase::failed,
             "场景准备预算耗尽：候选可用 " + std::to_string(local_remaining) +
                 " 字节，Application 可用 " + std::to_string(resources_.available_memory_bytes()) +
                 " 字节（含活动场景、候选及旧帧）；未启动下一批");
      return;
    }
    const auto submitted = value.assets->submit_assets(
        std::span(value.requested).subspan(value.cursor, value.batch_count), progress.session,
        progress.revision, child, false,
        std::min(remaining, asset_internal::texture_load_service::maximum_candidate_bytes));
    if (submitted == GNEISS_ERROR_NOT_READY) {
      return;
    }
    if (submitted != GNEISS_SUCCESS) {
      finish(submitted, scene_load_phase::failed);
      return;
    }
    value.batch_pending = true;
    return;
  }
  if (progress.phase == scene_load_phase::instantiating) {
    bool complete{};
    const auto built = value.builder->advance(complete);
    progress.completed = value.builder->completed_nodes();
    if (built != GNEISS_SUCCESS) {
      finish(built, scene_load_phase::failed);
      return;
    }
    if (complete) {
      progress.phase = scene_load_phase::ready;
    }
  }
}

} // namespace gneiss::application_internal
