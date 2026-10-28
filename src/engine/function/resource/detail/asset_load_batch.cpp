// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/resource/detail/asset_load_batch.hpp"
#include "engine/core/diagnostics/profiling.hpp"
#include "engine/core/result.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/detail/resource_service_impl.hpp"
#include "engine/function/resource/resource_service.hpp"
#include "engine/resource/asset_uri.hpp"
#include "engine/resource/material/material_description.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace gneiss::engine {
struct asset_load_batch::pending {
  std::variant<std::monostate, asset_data_request<mesh_data>, asset_data_request<texture_data>,
               asset_request<material_description>>
      request;
  decoded_asset decoded;
  asset_diagnostic diagnostic;
  double decode_ms{};
  detail::asset_update item;
  std::vector<render_asset_ref> targets;
  asset_load_completion completion;
  std::size_t explicit_count{};
  std::size_t cursor{};
  std::size_t limit{};
  std::uint64_t sequence{};
  bool reload{};
  bool cancelled{};
  engine::result summary{engine::result::success};
  engine::result fatal{engine::result::success};
  std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
};

asset_load_batch::asset_load_batch(task_executor& executor, resource_service& assets,
                                   texture_prepare_profile profile,
                                   std::shared_ptr<progress_notification> notification)
    : assets_(assets), profile_(profile), notification_(std::move(notification)) {
  assets_.assets().attach_task_executor(executor);
}
asset_load_batch::~asset_load_batch() { request_stop(); }
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
  value.completion.prepare_ms += value.decode_ms;
  if (!value.diagnostic.message.empty()) {
    value.completion.message = std::move(value.diagnostic.message);
    value.completion.path = std::move(value.diagnostic.path);
    value.completion.byte_offset = value.diagnostic.byte_offset;
  }
  if (!applied && value.completion.message.empty()) {
    value.completion.message = "资源加载失败：" + item.source.uri;
    value.completion.path = item.source.uri;
  }
  value.item = {};
  value.request = std::monostate{};
  value.decoded = {};
  value.diagnostic = {};
  value.decode_ms = 0;
  ++value.cursor;
  if (notification_) {
    notification_->notify();
  }
}
void asset_load_batch::finish(engine::result result, asset_load_state state) {
  auto& value = *pending_;
  value.completion.result = result;
  value.completion.state = state;
  value.completion.prepare_ms += value.decode_ms;
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
    }
  } catch (...) {
    if (pending_) {
      pending_->fatal = engine::result::internal;
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
  const auto limit = std::min(maximum_bytes, value.limit - value.completion.prepared_bytes);
  value.decoded.uri = source.uri;
  result status;
  switch (source.type) {
  case asset_type::mesh:
    status = assets_.assets().load_mesh_data(
        source.uri, value.request.emplace<asset_data_request<mesh_data>>(), limit);
    break;
  case asset_type::texture:
    status = assets_.assets().load_texture_data(
        source.uri, profile_, value.request.emplace<asset_data_request<texture_data>>(), limit);
    break;
  case asset_type::material:
    status = assets_.assets().load(
        source.uri, value.request.emplace<asset_request<material_description>>(), value.reload);
    break;
  default:
    status = result::invalid_argument;
    break;
  }
  if (status.failed()) {
    finish_item(status, false);
    return false;
  }
  return true;
}
result asset_load_batch::take_item(pending& value) {
  meta::serialization_diagnostic diagnostic;
  asset_data_metrics metrics;
  const auto limit = std::min(maximum_bytes, value.limit - value.completion.prepared_bytes);
  const auto status = std::visit(
      [&](auto& request) -> result {
        using T = std::decay_t<decltype(request)>;
        if constexpr (std::is_same_v<T, asset_data_request<mesh_data>>) {
          mesh_data data;
          const auto result = request.take(data, &diagnostic, &metrics);
          if (result.ok())
            value.decoded.content = std::move(data);
          return result;
        } else if constexpr (std::is_same_v<T, asset_data_request<texture_data>>) {
          texture_data data;
          const auto result = request.take(data, &diagnostic, &metrics);
          if (result.ok())
            value.decoded.content = std::move(data);
          return result;
        } else if constexpr (std::is_same_v<T, asset_request<material_description>>) {
          asset_ref<const material_description> description;
          const auto status = request.take(description, &diagnostic);
          if (status.failed())
            return status;
          const auto adopted =
              adopt_material_asset(value.decoded.uri, std::move(description), value.decoded,
                                   value.diagnostic, limit, sizeof(material_data));
          metrics.bytes = value.decoded.bytes;
          return adopted;
        } else
          return result::invalid_state;
      },
      value.request);
  if (status.ok()) {
    value.decoded.sources = std::move(metrics.sources);
    value.decoded.bytes = metrics.bytes;
    value.decode_ms = metrics.decode_ms;
    value.completion.queue_ms += metrics.queue_ms;
  } else if (!diagnostic.message.empty()) {
    value.diagnostic = {status, diagnostic.byte_offset, std::move(diagnostic.path),
                        std::move(diagnostic.message)};
  }
  return status;
}

bool asset_load_batch::prepare_item_update(pending& value) {
  auto& prepared = value.decoded;
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
  return true;
}
void asset_load_batch::advance_impl() {
  if (!pending_) {
    return;
  }
  auto& value = *pending_;
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
  if (std::holds_alternative<std::monostate>(value.request) && !start_item(value))
    return;
  const auto status = take_item(value);
  if (status == result::not_ready)
    return;
  if (status.failed()) {
    finish_item(status, false);
    return;
  }
  if (!prepare_item_update(value))
    return;
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
  pending_->request = std::monostate{};
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
