// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/texture_load_service.hpp"
#include "engine/core/diagnostics/profiling.hpp"
#include "engine/core/diagnostics/task_submission.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/vfs/asset_uri.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace gneiss::engine::render {
struct texture_load_service::pending {
  struct cpu_result {
    prepared_render_batch batch;
    std::unique_ptr<engine::resource::asset_preparation> preparation;
    bool complete{};
    gneiss::engine::core::result result{gneiss::engine::core::result::success};
    std::string message;
    double milliseconds{};
  };
  std::shared_ptr<cpu_result> cpu;
  render_asset_loader::asset_upload item;
  std::vector<render_asset_lease> targets;
  texture_load_completion completion;
  engine::core::tasks::task_handle task;
  std::size_t explicit_count{};
  std::size_t cursor{};
  std::size_t limit{};
  std::uint64_t sequence{};
  std::uint64_t upload{};
  std::size_t upload_reserved_bytes{};
  bool reload{};
  bool prepared{};
  bool ever_uploaded{};
  bool in_flight{};
  bool needs_discard{};
  bool discarding{};
  bool cancelled{};
  gneiss::engine::core::result operation{gneiss::engine::core::result::success};
  gneiss::engine::core::result summary{gneiss::engine::core::result::success};
  gneiss::engine::core::result fatal{gneiss::engine::core::result::success};
  std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
};

texture_load_service::texture_load_service(
    engine::core::tasks::task_executor& executor, engine::resource::virtual_file_system file_system,
    render_asset_loader& loader, texture_upload_backend backend,
    std::shared_ptr<engine::core::progress_notification> notification)
    : executor_(executor), scope_(executor.make_scope()), file_system_(std::move(file_system)),
      loader_(loader), backend_(std::move(backend)), notification_(std::move(notification)) {
  if (scope_.id == 0U || !backend_.begin || !backend_.poll || !backend_.discard ||
      !backend_.flush) {
    if (scope_.id != 0U) {
      (void)executor_.close_scope(scope_);
    }
    throw std::invalid_argument("资产服务执行或上传后端无效");
  }
}
texture_load_service::~texture_load_service() {
  try {
    request_stop();
    (void)executor_.close_scope(scope_);
    if (pending_ && !pending_->in_flight && !pending_->needs_discard) {
      pending_.reset();
    }
    while (pending_) {
      backend_.flush();
      advance();
    }
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

gneiss::engine::core::result texture_load_service::submit(std::span<const std::string> uris,
                                                          std::uint64_t session,
                                                          std::uint64_t revision,
                                                          std::uint64_t& request, bool reload) {
  request = 0U;
  if (uris.size() > maximum_batch) {
    return gneiss::engine::core::result::invalid_argument;
  }
  std::vector<render_asset_reload> sources;
  for (const auto& uri : uris) {
    sources.push_back({.uri = uri, .type = render_asset_type::texture});
  }
  return submit_assets(sources, session, revision, request, reload);
}
gneiss::engine::core::result texture_load_service::submit_assets(
    std::span<const render_asset_reload> sources, std::uint64_t session, std::uint64_t revision,
    std::uint64_t& request, bool reload, std::size_t prepare_limit) {
  request = 0U;
  if (std::this_thread::get_id() != owner_ || stopping_) {
    return gneiss::engine::core::result::invalid_state;
  }
  if (busy()) {
    return gneiss::engine::core::result::not_ready;
  }
  if (sources.empty() || sources.size() > maximum_assets || session == 0U || revision == 0U ||
      prepare_limit == 0U || prepare_limit > maximum_prepared_bytes) {
    return gneiss::engine::core::result::invalid_argument;
  }
  std::set<std::string> unique;
  for (const auto& source : sources) {
    if (engine::resource::validate_uri(source.uri) != gneiss::engine::core::result::success ||
        !unique.insert(source.uri).second ||
        (source.type != render_asset_type::mesh && source.type != render_asset_type::material &&
         source.type != render_asset_type::texture)) {
      return gneiss::engine::core::result::invalid_argument;
    }
  }
  auto value = std::make_unique<pending>();
  value->completion.request = ++sequence_;
  value->completion.session = session;
  value->completion.revision = revision;
  value->completion.profile = backend_.profile;
  value->explicit_count = sources.size();
  value->limit = prepare_limit;
  value->reload = reload;
  value->sequence = loader_.next_update_sequence();
  value->targets.reserve(maximum_assets);
  value->completion.results.reserve(maximum_assets);
  value->completion.assets.reserve(maximum_assets);
  bool cached = !reload;
  for (const auto& source : sources) {
    render_asset_lease target;
    const auto ready = loader_.acquire_cached(source, target, backend_.profile);
    cached = cached && ready == gneiss::engine::core::result::success;
    const auto reserved = loader_.reserve_asset(source, target);
    if (reserved != gneiss::engine::core::result::success) {
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
      item.result = gneiss::engine::core::result::success;
      item.applied = true;
    }
    value->completion.result = gneiss::engine::core::result::success;
    value->completion.state = texture_load_state::applied;
    request = value->completion.request;
    completed_ = std::move(value->completion);
    return gneiss::engine::core::result::success;
  }
  // 服务槽位接受整个请求；执行器暂满时由后续推进重试调度。
  for (std::size_t index = 0; index < value->targets.size(); ++index) {
    render_asset_lease ready;
    if (reload || loader_.acquire_cached(sources[index], ready, backend_.profile) !=
                      gneiss::engine::core::result::success) {
      (void)loader_.claim_update(value->targets[index], value->sequence);
    }
  }
  request = value->completion.request;
  pending_ = std::move(value);
  return gneiss::engine::core::result::success;
}

engine::core::tasks::submit_result texture_load_service::submit_preparation(pending& value) {
  return engine::core::diagnostics::submit_observed(
      executor_, {.name = "render_assets.prepare", .scope = scope_, .notification = notification_},
      [cpu = value.cpu](const engine::core::tasks::task_context& context) {
        GNEISS_PROFILE_SCOPE("asset.prepare.step");
        const auto started = std::chrono::steady_clock::now();
        asset_diagnostic diagnostic;
        cpu->result = cpu->preparation->advance(
            std::size_t{context.allows_blocking_wait() ? 16U : 4U} * 1024U * 1024U,
            [&] { return context.stop_requested(); }, cpu->batch, diagnostic, cpu->complete,
            std::chrono::milliseconds(context.allows_blocking_wait() ? 4 : 0));
        if (cpu->complete) {
          cpu->message = std::move(diagnostic.message);
          cpu->preparation.reset();
        }
        cpu->milliseconds +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
        return engine::core::tasks::task_outcome{
            .state = context.stop_requested() ? engine::core::tasks::task_state::cancelled
                     : cpu->result == gneiss::engine::core::result::success
                         ? engine::core::tasks::task_state::succeeded
                         : engine::core::tasks::task_state::failed,
        };
      },
      value.task);
}
bool texture_load_service::advance_preparation(pending& value) {
  if (value.task.id != 0U) {
    std::vector<engine::core::tasks::task_completion> completions;
    if (executor_.poll(scope_, completions, 1U) == 0U) {
      return false;
    }
    value.task = {};
    value.completion.queue_ms += completions.front().queue_ms;
    if (value.cpu &&
        completions.front().outcome.state != engine::core::tasks::task_state::succeeded &&
        value.cpu->result == gneiss::engine::core::result::success) {
      value.cpu->result = gneiss::engine::core::result::internal;
      value.cpu->complete = true;
    }
  }
  return true;
}
gneiss::engine::core::result
texture_load_service::enqueue_dependencies(pending& value, const prepared_render_asset& asset) {
  for (const auto& uri : asset.texture_uris) {
    if (uri.empty()) {
      continue;
    }
    const auto found = std::ranges::find_if(
        value.completion.results, [&](const auto& item) { return item.source.uri == uri; });
    if (found != value.completion.results.end()) {
      if (found->source.type != render_asset_type::texture) {
        return gneiss::engine::core::result::invalid_argument;
      }
      continue;
    }
    if (value.completion.results.size() == maximum_assets) {
      return gneiss::engine::core::result::invalid_argument;
    }
    render_asset_reload source{.uri = uri, .type = render_asset_type::texture};
    render_asset_lease target;
    const auto reserved = loader_.reserve_asset(source, target);
    if (reserved != gneiss::engine::core::result::success) {
      return reserved;
    }
    value.targets.push_back(std::move(target));
    value.completion.results.push_back({.source = std::move(source)});
  }
  return gneiss::engine::core::result::success;
}
void texture_load_service::finish_item(gneiss::engine::core::result result, bool applied) {
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
    }
  }
  if (!applied && value.completion.message.empty()) {
    value.completion.message = "资源加载失败：" + item.source.uri;
  }
  value.item = {};
  value.cpu.reset();
  value.prepared = false;
  value.operation = gneiss::engine::core::result::success;
  ++value.cursor;
  if (notification_) {
    notification_->notify();
  }
}
void texture_load_service::finish(gneiss::engine::core::result result, texture_load_state state) {
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
    if (!item.applied && item.result == gneiss::engine::core::result::not_ready) {
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
      pending_->fatal = gneiss::engine::core::result::out_of_memory;
      (void)executor_.cancel(pending_->task);
    }
  } catch (...) {
    if (pending_) {
      pending_->fatal = gneiss::engine::core::result::internal;
      (void)executor_.cancel(pending_->task);
    }
  }
}
void texture_load_service::poll_upload(pending& value) {
  auto result = gneiss::engine::core::result::success;
  if (!backend_.poll(value.upload, result)) {
    return;
  }
  value.in_flight = false;
  value.upload_reserved_bytes = 0U;
  if (value.discarding) {
    value.discarding = value.needs_discard = false;
    finish_item(value.operation, false);
    return;
  }
  if (backend_.elapsed_ms) {
    value.completion.upload_ms += backend_.elapsed_ms();
  }
  if (result != gneiss::engine::core::result::success) {
    finish_item(result, false);
    return;
  }
  value.operation = value.cancelled ? gneiss::engine::core::result::invalid_state : value.fatal;
  if (value.operation == gneiss::engine::core::result::success) {
    value.operation = loader_.complete_upload(value.item);
  }
  if (value.operation == gneiss::engine::core::result::success) {
    finish_item(gneiss::engine::core::result::success, true);
    return;
  }
  value.needs_discard = true;
}
bool texture_load_service::start_item(pending& value) {
  const auto& source = value.completion.results[value.cursor].source;
  render_asset_lease cached;
  if ((!value.reload || value.cursor >= value.explicit_count) &&
      loader_.acquire_cached(source, cached, backend_.profile) ==
          gneiss::engine::core::result::success) {
    finish_item(gneiss::engine::core::result::success, true);
    return false;
  }
  if (!loader_.claim_update(value.targets[value.cursor], value.sequence)) {
    finish_item(gneiss::engine::core::result::invalid_state, false);
    return false;
  }
  if (value.completion.prepared_bytes >= value.limit) {
    finish_item(gneiss::engine::core::result::out_of_memory, false);
    return false;
  }
  value.cpu = std::make_shared<pending::cpu_result>();
  value.cpu->preparation = std::make_unique<engine::resource::asset_preparation>(
      file_system_, std::span(&source, 1U),
      engine::resource::asset_preparation::limits{
          .material_bytes = sizeof(material_resource),
          .maximum_assets = 1U,
          .maximum_bytes = value.limit - value.completion.prepared_bytes,
          .include_dependencies = false,
      },
      backend_.profile);
  return true;
}
bool texture_load_service::prepare_item_upload(pending& value) {
  if (value.cpu->batch.assets.size() != 1U) {
    finish_item(gneiss::engine::core::result::internal, false);
    return false;
  }
  auto& prepared = value.cpu->batch.assets.front();
  const auto dependencies = enqueue_dependencies(value, prepared);
  if (dependencies != gneiss::engine::core::result::success) {
    finish_item(dependencies, false);
    return false;
  }
  value.completion.prepared_bytes += prepared.bytes;
  const auto staged = loader_.prepare_upload(std::move(prepared), value.item,
                                             value.targets[value.cursor], value.sequence);
  if (staged != gneiss::engine::core::result::success) {
    finish_item(staged, false);
    return false;
  }
  if (backend_.estimate_bytes) {
    value.item.data.bytes = backend_.estimate_bytes(value.item.data);
  }
  if (value.item.data.bytes > maximum_upload_bytes) {
    finish_item(gneiss::engine::core::result::out_of_memory, false);
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
  if (value.in_flight) {
    poll_upload(value);
    return;
  }
  if (value.needs_discard) {
    const auto result = backend_.discard({value.item.data}, value.upload);
    if (result == gneiss::engine::core::result::not_ready) {
      return;
    }
    if (result != gneiss::engine::core::result::success) {
      finish(result, texture_load_state::failed);
      return;
    }
    value.in_flight = value.discarding = true;
    return;
  }
  if (!advance_preparation(value)) {
    return;
  }
  if (value.cancelled || value.fatal != gneiss::engine::core::result::success) {
    finish(value.cancelled ? gneiss::engine::core::result::invalid_state : value.fatal,
           value.cancelled ? texture_load_state::cancelled : texture_load_state::failed);
    return;
  }
  if (value.cursor == value.completion.results.size()) {
    finish(value.summary, value.summary == gneiss::engine::core::result::success
                              ? texture_load_state::applied
                              : texture_load_state::failed);
    return;
  }
  if (!value.cpu && !start_item(value)) {
    return;
  }
  if (!value.cpu->complete) {
    const auto accepted = submit_preparation(value);
    if (accepted != engine::core::tasks::submit_result::success &&
        accepted != engine::core::tasks::submit_result::full) {
      finish_item(gneiss::engine::core::result::invalid_state, false);
    }
    return;
  }
  if (value.cpu->result != gneiss::engine::core::result::success) {
    finish_item(value.cpu->result, false);
    return;
  }
  if (!value.prepared && !prepare_item_upload(value)) {
    return;
  }
  const auto uploaded = backend_.begin({value.item.data}, value.upload);
  if (uploaded == gneiss::engine::core::result::not_ready) {
    return;
  }
  if (uploaded != gneiss::engine::core::result::success) {
    finish_item(uploaded, false);
    return;
  }
  value.upload_reserved_bytes = value.item.data.bytes;
  value.completion.peak_upload_bytes =
      std::max(value.completion.peak_upload_bytes, value.upload_reserved_bytes);
  value.ever_uploaded = value.in_flight = true;
}
std::span<const render_asset_lease> texture_load_service::requested_assets() const {
  check_owner();
  if (pending_) {
    return std::span(pending_->targets).first(pending_->explicit_count);
  }
  return completed_ ? std::span<const render_asset_lease>{completed_->assets}
                    : std::span<const render_asset_lease>{};
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
  return pending_ && !pending_->ever_uploaded && cancel_unpublished();
}
bool texture_load_service::cancel_unpublished() {
  check_owner();
  if (!pending_) {
    return false;
  }
  pending_->cancelled = true;
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
      .state = value.ever_uploaded ? texture_load_state::uploading : texture_load_state::preparing,
      .completed_assets = static_cast<std::size_t>(std::ranges::count_if(
          value.completion.results, [](const auto& item) { return item.applied; })),
      .total_assets = value.completion.results.size(),
      .can_cancel = !value.ever_uploaded,
      .upload_reserved_bytes = value.upload_reserved_bytes,
  };
  return true;
}
} // namespace gneiss::engine::render
