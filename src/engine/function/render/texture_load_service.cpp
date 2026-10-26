// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/texture_load_service.hpp"
#include "engine/core/diagnostics/profiling.hpp"
#include "engine/core/diagnostics/task_submission.hpp"
#include "engine/core/result.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/asset_manager.hpp"
#include "engine/resource/loaders/asset_parsing.hpp"
#include "engine/resource/types/material_description.hpp"
#include "engine/resource/asset_uri.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace gneiss::engine {
struct texture_load_service::pending {
  struct cpu_result {
    prepared_render_batch batch;
    asset_ref<const material_description> material;
    asset_preparation_source source;
    texture_description description;
    file_bytes description_bytes, payload_bytes;
    std::vector<prepared_source> inputs;
    texture_prepare_profile profile;
    std::size_t limit{};
    bool reading{}, parse_description{}, needs_payload{};
    bool complete{};
    gneiss::engine::result result{gneiss::engine::result::success};
    std::string message;
    std::string path;
    std::size_t byte_offset{};
    double milliseconds{};
  };
  std::shared_ptr<cpu_result> cpu;
  std::shared_ptr<file_service> files;
  file_request_id read_request;
  ~pending() {
    if (files && read_request.value != 0U)
      (void)files->cancel(read_request);
  }
  asset_request<material_description> material_request;
  render_asset_loader::asset_update item;
  std::vector<render_asset_ref> targets;
  texture_load_completion completion;
  gneiss::engine::task_handle task;
  std::size_t explicit_count{};
  std::size_t cursor{};
  std::size_t limit{};
  std::uint64_t sequence{};
  bool reload{};
  bool prepared{};
  bool cancelled{};
  gneiss::engine::result summary{gneiss::engine::result::success};
  gneiss::engine::result fatal{gneiss::engine::result::success};
  std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
};

texture_load_service::texture_load_service(
    gneiss::engine::task_executor& executor, render_asset_loader& loader,
    texture_prepare_profile profile,
    std::shared_ptr<gneiss::engine::progress_notification> notification)
    : executor_(executor), scope_(executor.make_scope()), loader_(loader), profile_(profile),
      notification_(std::move(notification)) {
  if (scope_.id == 0U) {
    if (scope_.id != 0U) {
      (void)executor_.close_scope(scope_);
    }
    throw std::invalid_argument("资产服务执行器无效");
  }
  try {
    loader_.assets().attach_task_executor(executor_);
  } catch (...) {
    (void)executor_.close_scope(scope_);
    throw;
  }
}
texture_load_service::~texture_load_service() {
  try {
    request_stop();
    (void)executor_.close_scope(scope_);
    pending_.reset();
    loader_.release_unused();
  } catch (...) {
    std::terminate();
  }
}
void texture_load_service::check_owner() const {
  if (std::this_thread::get_id() != owner_) {
    throw std::logic_error("资产服务线程错误");
  }
}

gneiss::engine::result texture_load_service::submit(std::span<const std::string> uris,
                                                    std::uint64_t session, std::uint64_t revision,
                                                    std::uint64_t& request, bool reload) {
  request = 0U;
  if (uris.size() > maximum_batch) {
    return gneiss::engine::result::invalid_argument;
  }
  std::vector<render_asset_reload> sources;
  for (const auto& uri : uris) {
    sources.push_back({.uri = uri, .type = render_asset_type::texture});
  }
  return submit_assets(sources, session, revision, request, reload);
}
gneiss::engine::result texture_load_service::submit_assets(
    std::span<const render_asset_reload> sources, std::uint64_t session, std::uint64_t revision,
    std::uint64_t& request, bool reload, std::size_t prepare_limit) {
  request = 0U;
  if (std::this_thread::get_id() != owner_ || stopping_) {
    return gneiss::engine::result::invalid_state;
  }
  if (busy()) {
    return gneiss::engine::result::not_ready;
  }
  if (sources.empty() || sources.size() > maximum_assets || session == 0U || revision == 0U ||
      prepare_limit == 0U || prepare_limit > maximum_prepared_bytes) {
    return gneiss::engine::result::invalid_argument;
  }
  std::set<std::string> unique;
  for (const auto& source : sources) {
    if (gneiss::engine::validate_uri(source.uri) != gneiss::engine::result::success ||
        !unique.insert(source.uri).second ||
        (source.type != render_asset_type::mesh && source.type != render_asset_type::material &&
         source.type != render_asset_type::texture)) {
      return gneiss::engine::result::invalid_argument;
    }
  }
  auto value = std::make_unique<pending>();
  value->completion.request = ++sequence_;
  value->completion.session = session;
  value->completion.revision = revision;
  value->completion.profile = profile_;
  value->explicit_count = sources.size();
  value->limit = prepare_limit;
  value->reload = reload;
  value->sequence = loader_.next_update_sequence();
  value->targets.reserve(maximum_assets);
  value->completion.results.reserve(maximum_assets);
  value->completion.assets.reserve(maximum_assets);
  bool cached = !reload;
  for (const auto& source : sources) {
    render_asset_ref target;
    const auto ready = loader_.acquire_cached(source, target, profile_);
    cached = cached && ready == gneiss::engine::result::success;
    const auto reserved = loader_.reserve_asset(source, target);
    if (reserved != gneiss::engine::result::success) {
      value.reset();
      loader_.release_unused();
      return reserved;
    }
    value->targets.push_back(std::move(target));
    value->completion.results.push_back({.source = source});
  }
  if (cached) {
    value->completion.assets = value->targets;
    for (auto& item : value->completion.results) {
      item.result = gneiss::engine::result::success;
      item.applied = true;
    }
    value->completion.result = gneiss::engine::result::success;
    value->completion.state = texture_load_state::applied;
    request = value->completion.request;
    completed_ = std::move(value->completion);
    return gneiss::engine::result::success;
  }
  // 服务槽位接受整个请求；执行器暂满时由后续推进重试调度。
  for (std::size_t index = 0; index < value->targets.size(); ++index) {
    render_asset_ref ready;
    if (reload || loader_.acquire_cached(sources[index], ready, profile_) !=
                      gneiss::engine::result::success) {
      (void)loader_.claim_update(value->targets[index], value->sequence);
    }
  }
  request = value->completion.request;
  pending_ = std::move(value);
  return gneiss::engine::result::success;
}

gneiss::engine::submit_result texture_load_service::submit_preparation(pending& value) {
  return gneiss::engine::submit_observed(
      executor_, {.name = "render_assets.prepare", .scope = scope_, .notification = notification_},
      [cpu = value.cpu](const gneiss::engine::task_context& context) {
        GNEISS_PROFILE_SCOPE("asset.prepare.step");
        const auto started = std::chrono::steady_clock::now();
        if (context.stop_requested()) {
          cpu->result = result::cancelled;
          cpu->complete = true;
          return task_outcome{.state = task_state::cancelled};
        }
        asset_diagnostic diagnostic;
        if (cpu->parse_description) {
          cpu->result = detail::parse_texture(cpu->description_bytes, cpu->description, diagnostic);
          cpu->parse_description = false;
          cpu->needs_payload = cpu->result.ok();
          cpu->complete = cpu->result.failed();
        } else {
          if (!cpu->inputs.empty()) {
            cpu->inputs.front().bytes = cpu->description_bytes.size();
            cpu->inputs.front().digest = sha256(cpu->description_bytes);
          }
          if (cpu->inputs.size() > 1U) {
            cpu->inputs.back().bytes = cpu->payload_bytes.size();
            cpu->inputs.back().digest = sha256(cpu->payload_bytes);
          }
          prepared_asset prepared;
          cpu->result = decode_loaded_asset(cpu->source, std::move(cpu->description_bytes),
                                            cpu->description.uri, std::move(cpu->payload_bytes),
                                            prepared, diagnostic, cpu->limit, cpu->profile,
                                            cpu->material, sizeof(material_data));
          if (cpu->result.ok()) {
            prepared.sources = std::move(cpu->inputs);
            cpu->batch.bytes = prepared.bytes;
            cpu->batch.assets.push_back(std::move(prepared));
          }
          cpu->complete = true;
        }
        cpu->message = std::move(diagnostic.message);
        cpu->path = std::move(diagnostic.path);
        cpu->byte_offset = diagnostic.byte_offset;
        cpu->milliseconds +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
        return gneiss::engine::task_outcome{
            .state = context.stop_requested() ? gneiss::engine::task_state::cancelled
                     : cpu->result == gneiss::engine::result::success
                         ? gneiss::engine::task_state::succeeded
                         : gneiss::engine::task_state::failed,
        };
      },
      value.task);
}
bool texture_load_service::advance_preparation(pending& value) {
  if (value.task.id != 0U) {
    std::vector<gneiss::engine::task_completion> completions;
    if (executor_.poll(scope_, completions, 1U) == 0U) {
      return false;
    }
    value.task = {};
    value.completion.queue_ms += completions.front().queue_ms;
    if (value.cpu && completions.front().outcome.state != gneiss::engine::task_state::succeeded &&
        value.cpu->result == gneiss::engine::result::success) {
      value.cpu->result = gneiss::engine::result::internal;
      value.cpu->complete = true;
    }
  }
  return true;
}
gneiss::engine::result
texture_load_service::enqueue_dependencies(pending& value, const prepared_render_asset& asset) {
  for (const auto& uri : asset.texture_uris) {
    if (uri.empty()) {
      continue;
    }
    const auto found = std::ranges::find_if(
        value.completion.results, [&](const auto& item) { return item.source.uri == uri; });
    if (found != value.completion.results.end()) {
      if (found->source.type != render_asset_type::texture) {
        return gneiss::engine::result::invalid_argument;
      }
      continue;
    }
    if (value.completion.results.size() == maximum_assets) {
      return gneiss::engine::result::invalid_argument;
    }
    render_asset_reload source{.uri = uri, .type = render_asset_type::texture};
    render_asset_ref target;
    const auto reserved = loader_.reserve_asset(source, target);
    if (reserved != gneiss::engine::result::success) {
      return reserved;
    }
    value.targets.push_back(std::move(target));
    value.completion.results.push_back({.source = std::move(source)});
  }
  return gneiss::engine::result::success;
}
void texture_load_service::finish_item(gneiss::engine::result result, bool applied) {
  auto& value = *pending_;
  auto& item = value.completion.results[value.cursor];
  item.result = result;
  item.applied = applied;
  if (applied) {
    value.completion.assets.push_back(value.targets[value.cursor]);
  } else {
    value.summary = result;
  }
  if (value.cpu) {
    value.completion.prepare_ms += value.cpu->milliseconds;
    if (!value.cpu->message.empty()) {
      value.completion.message = value.cpu->message;
      value.completion.path = value.cpu->path;
      value.completion.byte_offset = value.cpu->byte_offset;
    }
  }
  if (!applied && value.completion.message.empty()) {
    value.completion.message = "资源加载失败：" + item.source.uri;
    value.completion.path = item.source.uri;
  }
  value.item = {};
  if (value.files && value.read_request.value != 0U)
    (void)value.files->cancel(value.read_request);
  value.read_request = {};
  value.cpu.reset();
  value.material_request.cancel();
  value.prepared = false;
  ++value.cursor;
  if (notification_) {
    notification_->notify();
  }
}
void texture_load_service::finish(gneiss::engine::result result, texture_load_state state) {
  auto& value = *pending_;
  value.completion.result = result;
  value.completion.state = state;
  if (value.cpu) {
    value.completion.prepare_ms += value.cpu->milliseconds;
  }
  value.completion.commit_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - value.started)
          .count();
  for (auto& item : value.completion.results) {
    if (!item.applied && item.result == gneiss::engine::result::not_ready) {
      item.result = result;
    }
  }
  completed_ = std::move(value.completion);
  pending_.reset();
  loader_.release_unused();
  if (notification_) {
    notification_->notify();
  }
}
void texture_load_service::advance() {
  GNEISS_PROFILE_SCOPE("asset.advance");
  check_owner();
  try {
    advance_impl();
  } catch (const std::bad_alloc&) {
    if (pending_) {
      pending_->fatal = gneiss::engine::result::out_of_memory;
      (void)executor_.cancel(pending_->task);
    }
  } catch (...) {
    if (pending_) {
      pending_->fatal = gneiss::engine::result::internal;
      (void)executor_.cancel(pending_->task);
    }
  }
}
bool texture_load_service::start_item(pending& value) {
  const auto& source = value.completion.results[value.cursor].source;
  render_asset_ref cached;
  if ((!value.reload || value.cursor >= value.explicit_count) &&
      loader_.acquire_cached(source, cached, profile_) == gneiss::engine::result::success) {
    finish_item(gneiss::engine::result::success, true);
    return false;
  }
  if (!loader_.claim_update(value.targets[value.cursor], value.sequence)) {
    finish_item(gneiss::engine::result::invalid_state, false);
    return false;
  }
  if (value.completion.prepared_bytes >= value.limit) {
    finish_item(gneiss::engine::result::out_of_memory, false);
    return false;
  }
  value.cpu = std::make_shared<pending::cpu_result>();
  value.cpu->source = source;
  value.cpu->profile = profile_;
  value.cpu->limit = std::min(maximum_bytes, value.limit - value.completion.prepared_bytes);
  if (source.type == render_asset_type::material) {
    const auto status = loader_.assets().load(source.uri, value.material_request, value.reload);
    if (status.failed()) {
      finish_item(status, false);
      return false;
    }
    return true;
  }
  value.cpu->parse_description = source.type == render_asset_type::texture;
  value.files = loader_.assets().async_files();
  const auto status = start_read(value, source.uri, false);
  if (status.failed()) {
    finish_item(status, false);
    return false;
  }
  return true;
}
result texture_load_service::start_read(pending& value, std::string_view uri, bool payload) {
  if (!value.files)
    return result::invalid_state;
  prepared_source input;
  const auto identified = loader_.assets().identify(
      uri, static_cast<std::uint32_t>(value.cpu->source.type), input.identity);
  if (identified.failed())
    return identified;
  const auto used = payload ? value.cpu->description_bytes.size() : 0U;
  if (used >= value.cpu->limit)
    return result::out_of_memory;
  value.cpu->inputs.push_back(std::move(input));
  value.cpu->reading = true;
  const auto accepted = value.files->read(
      uri, {.max_bytes = value.cpu->limit - used},
      [weak = std::weak_ptr(value.cpu), payload](file_completion<file_bytes> completed) {
        if (auto cpu = weak.lock()) {
          cpu->reading = false;
          cpu->result = completed.status;
          cpu->complete = completed.status.failed();
          if (payload)
            cpu->payload_bytes = std::move(completed.data);
          else
            cpu->description_bytes = std::move(completed.data);
        }
      },
      value.read_request);
  if (accepted.failed())
    value.cpu->reading = false;
  return accepted;
}

bool texture_load_service::prepare_item_update(pending& value) {
  if (value.cpu->batch.assets.size() != 1U) {
    finish_item(gneiss::engine::result::internal, false);
    return false;
  }
  auto& prepared = value.cpu->batch.assets.front();
  const auto dependencies = enqueue_dependencies(value, prepared);
  if (dependencies != gneiss::engine::result::success) {
    finish_item(dependencies, false);
    return false;
  }
  value.completion.prepared_bytes += prepared.bytes;
  const auto staged = loader_.prepare_update(std::move(prepared), value.item,
                                             value.targets[value.cursor], value.sequence);
  if (staged != gneiss::engine::result::success) {
    finish_item(staged, false);
    return false;
  }
  value.prepared = true;
  return true;
}
void texture_load_service::advance_impl() {
  if (!pending_) {
    return;
  }
  auto& value = *pending_;
  if (!advance_preparation(value)) {
    return;
  }
  if (value.cancelled || value.fatal != gneiss::engine::result::success) {
    finish(value.cancelled ? gneiss::engine::result::invalid_state : value.fatal,
           value.cancelled ? texture_load_state::cancelled : texture_load_state::failed);
    return;
  }
  if (value.cursor == value.completion.results.size()) {
    finish(value.summary, value.summary == gneiss::engine::result::success
                              ? texture_load_state::applied
                              : texture_load_state::failed);
    return;
  }
  if (!value.cpu && !start_item(value)) {
    return;
  }
  if (value.cpu->reading)
    return;
  value.read_request = {};
  if (value.cpu->needs_payload) {
    value.cpu->needs_payload = false;
    const auto status = start_read(value, value.cpu->description.uri, true);
    if (status.failed())
      finish_item(status, false);
    return;
  }
  if (value.material_request) {
    asset_ref<const material_description> description;
    gneiss::engine::meta::serialization_diagnostic diagnostic;
    const auto status = value.material_request.take(description, &diagnostic);
    if (status == result::not_ready)
      return;
    if (status.failed()) {
      value.cpu->message = std::move(diagnostic.message);
      value.cpu->path = std::move(diagnostic.path);
      value.cpu->byte_offset = diagnostic.byte_offset;
      finish_item(status, false);
      return;
    }
    value.cpu->material = std::move(description);
  }
  if (!value.cpu->complete) {
    const auto accepted = submit_preparation(value);
    if (accepted != gneiss::engine::submit_result::success &&
        accepted != gneiss::engine::submit_result::full) {
      finish_item(gneiss::engine::result::invalid_state, false);
    }
    return;
  }
  if (value.cpu->result != gneiss::engine::result::success) {
    finish_item(value.cpu->result, false);
    return;
  }
  if (!value.prepared && !prepare_item_update(value)) {
    return;
  }
  const auto adopted = loader_.apply_update(value.item);
  finish_item(adopted, adopted.ok());
}
std::span<const render_asset_ref> texture_load_service::requested_assets() const {
  check_owner();
  if (pending_) {
    return std::span(pending_->targets).first(pending_->explicit_count);
  }
  return completed_ ? std::span<const render_asset_ref>{completed_->assets}
                    : std::span<const render_asset_ref>{};
}
bool texture_load_service::take(texture_load_completion& output) {
  check_owner();
  if (!completed_) {
    return false;
  }
  output = std::move(*completed_);
  completed_.reset();
  return true;
}
bool texture_load_service::cancel() {
  check_owner();
  return cancel_unpublished();
}
bool texture_load_service::cancel_unpublished() {
  check_owner();
  if (!pending_) {
    return false;
  }
  pending_->cancelled = true;
  if (pending_->files && pending_->read_request.value != 0U)
    (void)pending_->files->cancel(pending_->read_request);
  (void)executor_.cancel(pending_->task);
  return true;
}
void texture_load_service::request_stop() {
  stopping_ = true;
  (void)cancel_unpublished();
}
bool texture_load_service::stopped() const { return stopping_ && !pending_; }
bool texture_load_service::progress(asset_load_progress& output) const {
  check_owner();
  output = {};
  if (!pending_) {
    return false;
  }
  const auto& value = *pending_;
  output = {
      .request = value.completion.request,
      .session = value.completion.session,
      .revision = value.completion.revision,
      .state = texture_load_state::preparing,
      .completed_assets = static_cast<std::size_t>(std::ranges::count_if(
          value.completion.results, [](const auto& item) { return item.applied; })),
      .total_assets = value.completion.results.size(),
      .can_cancel = !value.cancelled,
  };
  return true;
}
} // namespace gneiss::engine
