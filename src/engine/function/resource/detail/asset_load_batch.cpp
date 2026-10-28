// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/resource/detail/asset_load_batch.hpp"
#include "engine/core/diagnostics/profiling.hpp"
#include "engine/core/diagnostics/task_submission.hpp"
#include "engine/core/result.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/detail/resource_service_impl.hpp"
#include "engine/function/resource/resource_service.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/asset_uri.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/texture/texture_decoder.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace gneiss::engine {
namespace {
struct mesh_decode_input {
  files::bytes bytes;
};
struct texture_decode_input {
  texture_description description;
  files::bytes description_bytes, payload_bytes;
  texture_prepare_profile profile;
  bool parse_description{true}, needs_payload{};
};
struct material_decode_input {
  asset_ref<const material_description> description;
};
using decode_input =
    std::variant<std::monostate, mesh_decode_input, texture_decode_input, material_decode_input>;
} // namespace
struct asset_load_batch::pending {
  struct cpu_result {
    prepared_batch batch;
    decode_input input;
    asset_source source;
    std::vector<prepared_source> inputs;
    std::size_t limit{};
    bool reading{};
    bool complete{};
    engine::result result{engine::result::success};
    std::string message;
    std::string path;
    std::size_t byte_offset{};
    double milliseconds{};
  };
  std::shared_ptr<cpu_result> cpu;
  std::shared_ptr<engine::files::service> files;
  engine::files::request_id read_request;
  ~pending() {
    if (files && read_request.value != 0U)
      (void)files->cancel(read_request);
  }
  asset_request<material_description> material_request;
  detail::asset_update item;
  std::vector<render_asset_ref> targets;
  asset_load_completion completion;
  task_handle task;
  std::size_t explicit_count{};
  std::size_t cursor{};
  std::size_t limit{};
  std::uint64_t sequence{};
  bool reload{};
  bool prepared{};
  bool cancelled{};
  engine::result summary{engine::result::success};
  engine::result fatal{engine::result::success};
  std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
};

asset_load_batch::asset_load_batch(task_executor& executor, resource_service& assets,
                                   texture_prepare_profile profile,
                                   std::shared_ptr<progress_notification> notification)
    : executor_(executor), scope_(executor.make_scope()), assets_(assets), profile_(profile),
      notification_(std::move(notification)) {
  if (scope_.id == 0U) {
    if (scope_.id != 0U) {
      (void)executor_.close_scope(scope_);
    }
    throw std::invalid_argument("资产批次执行器无效");
  }
  try {
    assets_.assets().attach_task_executor(executor_);
  } catch (...) {
    (void)executor_.close_scope(scope_);
    throw;
  }
}
asset_load_batch::~asset_load_batch() {
  try {
    request_stop();
    (void)executor_.close_scope(scope_);
    pending_.reset();
    assets_.collect_unused();
  } catch (...) {
    std::terminate();
  }
}
void asset_load_batch::check_owner() const {
  if (std::this_thread::get_id() != owner_) {
    throw std::logic_error("资产批次线程错误");
  }
}

engine::result asset_load_batch::submit(std::span<const std::string> uris, std::uint64_t session,
                                        std::uint64_t revision, std::uint64_t& request,
                                        bool reload) {
  request = 0U;
  if (uris.size() > maximum_batch) {
    return engine::result::invalid_argument;
  }
  std::vector<asset_source> sources;
  for (const auto& uri : uris) {
    sources.push_back({.uri = uri, .type = asset_type::texture});
  }
  return submit_assets(sources, session, revision, request, reload);
}
engine::result asset_load_batch::submit_assets(std::span<const asset_source> sources,
                                               std::uint64_t session, std::uint64_t revision,
                                               std::uint64_t& request, bool reload,
                                               std::size_t prepare_limit) {
  request = 0U;
  if (std::this_thread::get_id() != owner_ || stopping_) {
    return engine::result::invalid_state;
  }
  if (busy()) {
    return engine::result::not_ready;
  }
  if (sources.empty() || sources.size() > maximum_assets || session == 0U || revision == 0U ||
      prepare_limit == 0U || prepare_limit > maximum_prepared_bytes) {
    return engine::result::invalid_argument;
  }
  std::set<std::string> unique;
  for (const auto& source : sources) {
    if (engine::validate_uri(source.uri) != engine::result::success ||
        !unique.insert(source.uri).second ||
        (source.type != asset_type::mesh && source.type != asset_type::material &&
         source.type != asset_type::texture)) {
      return engine::result::invalid_argument;
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
  value->sequence = resource_service::implementation::next_update_sequence();
  value->targets.reserve(maximum_assets);
  value->completion.results.reserve(maximum_assets);
  value->completion.assets.reserve(maximum_assets);
  bool cached = !reload;
  for (const auto& source : sources) {
    render_asset_ref target;
    const auto ready = assets_.acquire_cached(source, target, profile_);
    cached = cached && ready == engine::result::success;
    const auto reserved = assets_.reserve_asset(source, target);
    if (reserved != engine::result::success) {
      value.reset();
      assets_.collect_unused();
      return reserved;
    }
    value->targets.push_back(std::move(target));
    value->completion.results.push_back({.source = source});
  }
  if (cached) {
    value->completion.assets = value->targets;
    for (auto& item : value->completion.results) {
      item.result = engine::result::success;
      item.applied = true;
    }
    value->completion.result = engine::result::success;
    value->completion.state = asset_load_state::applied;
    request = value->completion.request;
    completed_ = std::move(value->completion);
    return engine::result::success;
  }
  // 批次接受整个请求；执行器暂满时由后续推进重试调度。
  for (std::size_t index = 0; index < value->targets.size(); ++index) {
    render_asset_ref ready;
    if (reload ||
        assets_.acquire_cached(sources[index], ready, profile_) != engine::result::success) {
      (void)resource_service::implementation::claim_update(assets_, value->targets[index],
                                                           value->sequence);
    }
  }
  request = value->completion.request;
  pending_ = std::move(value);
  return engine::result::success;
}

submit_result asset_load_batch::submit_preparation(pending& value) {
  return engine::submit_observed(
      executor_, {.name = "render_assets.prepare", .scope = scope_, .notification = notification_},
      [cpu = value.cpu](const task_context& context) {
        GNEISS_PROFILE_SCOPE("asset.prepare.step");
        const auto started = std::chrono::steady_clock::now();
        if (context.stop_requested()) {
          cpu->result = result::cancelled;
          cpu->complete = true;
          return task_outcome{.state = task_state::cancelled};
        }
        asset_diagnostic diagnostic;
        decoded_asset prepared;
        cpu->result = std::visit(
            [&](auto& input) -> result {
              using T = std::decay_t<decltype(input)>;
              const auto record_source = [&](std::size_t index, const files::bytes& bytes) {
                cpu->inputs[index].bytes = bytes.size();
                cpu->inputs[index].digest = sha256(bytes);
              };
              cpu->complete = true;
              if constexpr (std::is_same_v<T, mesh_decode_input>) {
                record_source(0U, input.bytes);
                return decode_mesh_asset(cpu->source.uri, input.bytes, prepared, diagnostic,
                                         cpu->limit);
              } else if constexpr (std::is_same_v<T, texture_decode_input>) {
                if (input.parse_description) {
                  const auto status =
                      detail::parse_texture(input.description_bytes, input.description, diagnostic);
                  input.parse_description = false;
                  input.needs_payload = status.ok();
                  cpu->complete = status.failed();
                  return status;
                }
                record_source(0U, input.description_bytes);
                record_source(1U, input.payload_bytes);
                return decode_texture_asset(cpu->source.uri, input.description, input.payload_bytes,
                                            prepared, diagnostic, cpu->limit, input.profile);
              } else if constexpr (std::is_same_v<T, material_decode_input>) {
                return adopt_material_asset(cpu->source.uri, input.description, prepared,
                                            diagnostic, cpu->limit, sizeof(material_data));
              } else {
                return result::invalid_argument;
              }
            },
            cpu->input);
        if (cpu->complete && cpu->result.ok()) {
          prepared.sources = std::move(cpu->inputs);
          cpu->batch.bytes = prepared.bytes;
          cpu->batch.assets.push_back(std::move(prepared));
        }
        cpu->message = std::move(diagnostic.message);
        cpu->path = std::move(diagnostic.path);
        cpu->byte_offset = diagnostic.byte_offset;
        cpu->milliseconds +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
        return engine::task_outcome{
            .state = context.stop_requested()                 ? task_state::cancelled
                     : cpu->result == engine::result::success ? task_state::succeeded
                                                              : task_state::failed,
        };
      },
      value.task);
}
bool asset_load_batch::advance_preparation(pending& value) {
  if (value.task.id != 0U) {
    std::vector<task_completion> completions;
    if (executor_.poll(scope_, completions, 1U) == 0U) {
      return false;
    }
    value.task = {};
    value.completion.queue_ms += completions.front().queue_ms;
    if (value.cpu && completions.front().outcome.state != task_state::succeeded &&
        value.cpu->result == engine::result::success) {
      value.cpu->result = engine::result::internal;
      value.cpu->complete = true;
    }
  }
  return true;
}
engine::result asset_load_batch::enqueue_dependencies(pending& value, const decoded_asset& asset) {
  const auto* material = std::get_if<asset_ref<const material_description>>(&asset.content);
  if (!material)
    return result::success;
  if (!*material)
    return result::invalid_argument;
  for (const auto& uri : (*material)->texture_uris) {
    if (uri.empty()) {
      continue;
    }
    const auto found = std::ranges::find_if(
        value.completion.results, [&](const auto& item) { return item.source.uri == uri; });
    if (found != value.completion.results.end()) {
      if (found->source.type != asset_type::texture) {
        return engine::result::invalid_argument;
      }
      continue;
    }
    if (value.completion.results.size() == maximum_assets) {
      return engine::result::invalid_argument;
    }
    asset_source source{.uri = uri, .type = asset_type::texture};
    render_asset_ref target;
    const auto reserved = assets_.reserve_asset(source, target);
    if (reserved != engine::result::success) {
      return reserved;
    }
    value.targets.push_back(std::move(target));
    value.completion.results.push_back({.source = std::move(source)});
  }
  return engine::result::success;
}
void asset_load_batch::finish_item(engine::result result, bool applied) {
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
void asset_load_batch::finish(engine::result result, asset_load_state state) {
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
    if (!item.applied && item.result == engine::result::not_ready) {
      item.result = result;
    }
  }
  completed_ = std::move(value.completion);
  pending_.reset();
  assets_.collect_unused();
  if (notification_) {
    notification_->notify();
  }
}
void asset_load_batch::advance() {
  GNEISS_PROFILE_SCOPE("asset.advance");
  check_owner();
  try {
    advance_impl();
  } catch (const std::bad_alloc&) {
    if (pending_) {
      pending_->fatal = engine::result::out_of_memory;
      (void)executor_.cancel(pending_->task);
    }
  } catch (...) {
    if (pending_) {
      pending_->fatal = engine::result::internal;
      (void)executor_.cancel(pending_->task);
    }
  }
}
bool asset_load_batch::start_item(pending& value) {
  const auto& source = value.completion.results[value.cursor].source;
  render_asset_ref cached;
  if ((!value.reload || value.cursor >= value.explicit_count) &&
      assets_.acquire_cached(source, cached, profile_) == engine::result::success) {
    finish_item(engine::result::success, true);
    return false;
  }
  if (!resource_service::implementation::claim_update(assets_, value.targets[value.cursor],
                                                      value.sequence)) {
    finish_item(engine::result::invalid_state, false);
    return false;
  }
  if (value.completion.prepared_bytes >= value.limit) {
    finish_item(engine::result::out_of_memory, false);
    return false;
  }
  value.cpu = std::make_shared<pending::cpu_result>();
  value.cpu->source = source;
  value.cpu->limit = std::min(maximum_bytes, value.limit - value.completion.prepared_bytes);
  if (source.type == asset_type::material) {
    value.cpu->input.emplace<material_decode_input>();
    const auto status = assets_.assets().load(source.uri, value.material_request, value.reload);
    if (status.failed()) {
      finish_item(status, false);
      return false;
    }
    return true;
  }
  if (source.type == asset_type::texture)
    value.cpu->input.emplace<texture_decode_input>().profile = profile_;
  else
    value.cpu->input.emplace<mesh_decode_input>();
  value.files = assets_.assets().async_files();
  const auto status = start_read(value, source.uri, false);
  if (status.failed()) {
    finish_item(status, false);
    return false;
  }
  return true;
}
result asset_load_batch::start_read(pending& value, std::string_view uri, bool payload) {
  if (!value.files)
    return result::invalid_state;
  prepared_source input;
  const auto identified =
      assets_.identify(uri, static_cast<std::uint32_t>(value.cpu->source.type), input.identity);
  if (identified.failed())
    return identified;
  const auto* texture = std::get_if<texture_decode_input>(&value.cpu->input);
  if ((payload && !texture) ||
      (!texture && !std::holds_alternative<mesh_decode_input>(value.cpu->input)))
    return result::invalid_argument;
  const auto used = payload ? texture->description_bytes.size() : 0U;
  if (used >= value.cpu->limit)
    return result::out_of_memory;
  value.cpu->inputs.push_back(std::move(input));
  value.cpu->reading = true;
  const auto accepted = value.files->read(
      uri, {.max_bytes = value.cpu->limit - used},
      [weak = std::weak_ptr(value.cpu),
       payload](engine::files::completion<engine::files::bytes> completed) {
        if (auto cpu = weak.lock()) {
          cpu->reading = false;
          cpu->result = completed.status;
          cpu->complete = completed.status.failed();
          if (auto* texture = std::get_if<texture_decode_input>(&cpu->input)) {
            auto& bytes = payload ? texture->payload_bytes : texture->description_bytes;
            bytes = std::move(completed.data);
          } else {
            std::get<mesh_decode_input>(cpu->input).bytes = std::move(completed.data);
          }
        }
      },
      value.read_request);
  if (accepted.failed())
    value.cpu->reading = false;
  return accepted;
}

bool asset_load_batch::prepare_item_update(pending& value) {
  if (value.cpu->batch.assets.size() != 1U) {
    finish_item(engine::result::internal, false);
    return false;
  }
  auto& prepared = value.cpu->batch.assets.front();
  const auto dependencies = enqueue_dependencies(value, prepared);
  if (dependencies != engine::result::success) {
    finish_item(dependencies, false);
    return false;
  }
  value.completion.prepared_bytes += prepared.bytes;
  const auto staged = resource_service::implementation::prepare_update(
      assets_, std::move(prepared), value.item, value.targets[value.cursor], value.sequence);
  if (staged != engine::result::success) {
    finish_item(staged, false);
    return false;
  }
  value.prepared = true;
  return true;
}
void asset_load_batch::advance_impl() {
  if (!pending_) {
    return;
  }
  auto& value = *pending_;
  if (!advance_preparation(value)) {
    return;
  }
  if (value.cancelled || value.fatal != engine::result::success) {
    finish(value.cancelled ? engine::result::invalid_state : value.fatal,
           value.cancelled ? asset_load_state::cancelled : asset_load_state::failed);
    return;
  }
  if (value.cursor == value.completion.results.size()) {
    finish(value.summary, value.summary == engine::result::success ? asset_load_state::applied
                                                                   : asset_load_state::failed);
    return;
  }
  if (!value.cpu && !start_item(value)) {
    return;
  }
  if (value.cpu->reading)
    return;
  value.read_request = {};
  if (auto* texture = std::get_if<texture_decode_input>(&value.cpu->input);
      texture && texture->needs_payload) {
    texture->needs_payload = false;
    const auto status = start_read(value, texture->description.uri, true);
    if (status.failed())
      finish_item(status, false);
    return;
  }
  if (value.material_request) {
    asset_ref<const material_description> description;
    meta::serialization_diagnostic diagnostic;
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
    std::get<material_decode_input>(value.cpu->input).description = std::move(description);
  }
  if (!value.cpu->complete) {
    const auto accepted = submit_preparation(value);
    if (accepted != submit_result::success && accepted != submit_result::full) {
      finish_item(engine::result::invalid_state, false);
    }
    return;
  }
  if (value.cpu->result != engine::result::success) {
    finish_item(value.cpu->result, false);
    return;
  }
  if (!value.prepared && !prepare_item_update(value)) {
    return;
  }
  const auto adopted = resource_service::implementation::apply_update(assets_, value.item);
  finish_item(adopted, adopted.ok());
}
std::span<const render_asset_ref> asset_load_batch::requested_assets() const {
  check_owner();
  if (pending_) {
    return std::span(pending_->targets).first(pending_->explicit_count);
  }
  return completed_ ? std::span<const render_asset_ref>{completed_->assets}
                    : std::span<const render_asset_ref>{};
}
bool asset_load_batch::take(asset_load_completion& output) {
  check_owner();
  if (!completed_) {
    return false;
  }
  output = std::move(*completed_);
  completed_.reset();
  return true;
}
bool asset_load_batch::cancel() {
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
void asset_load_batch::request_stop() {
  stopping_ = true;
  (void)cancel();
}
bool asset_load_batch::stopped() const { return stopping_ && !pending_; }
bool asset_load_batch::progress(asset_load_progress& output) const {
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
      .state = asset_load_state::preparing,
      .completed_assets = static_cast<std::size_t>(std::ranges::count_if(
          value.completion.results, [](const auto& item) { return item.applied; })),
      .total_assets = value.completion.results.size(),
      .can_cancel = !value.cancelled,
  };
  return true;
}
} // namespace gneiss::engine
