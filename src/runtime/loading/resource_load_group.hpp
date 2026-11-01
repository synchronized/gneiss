// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/resource/resource_service.hpp"
#include "engine/resource/asset_uri.hpp"
#include "runtime/loading/resource_load_types.hpp"
#include <algorithm>
#include <chrono>
#include <optional>
#include <set>
namespace gneiss::runtime {
/** 场景/编辑器的请求汇总；不读文件、不解码、不创建或更新资源。 */
class resource_load_group final {
public:
  explicit resource_load_group(engine::resource_service& service) : service_(service) {}
  engine::texture_prepare_profile profile() const noexcept { return service_.profile(); }
  engine::error submit_assets(std::span<const engine::render_resource_source> sources,
                              std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                              bool reload = true) {
    using engine::error;
    request = 0;
    if (stopping_)
      return error::invalid_state;
    if (busy())
      return error::not_ready;
    if (sources.empty() || sources.size() > 256 || session == 0 || revision == 0)
      return error::invalid_argument;
    std::set<std::string> unique;
    for (const auto& source : sources)
      if (engine::validate_uri(source.uri).failed() || !unique.insert(source.uri).second ||
          (source.type != engine::render_resource_kind::mesh &&
           source.type != engine::render_resource_kind::texture &&
           source.type != engine::render_resource_kind::material))
        return error::invalid_argument;
    runtime::asset_load_completion next;
    for (const auto& source : sources)
      next.results.push_back({.source = source});
    next.request = ++sequence_;
    next.session = session;
    next.revision = revision;
    next.profile = profile();
    next.result = error::success;
    next.state = runtime::asset_load_state::preparing;
    completion_ = std::move(next);
    cursor_ = 0;
    reload_ = reload;
    started_ = std::chrono::steady_clock::now();
    request = completion_->request;
    return error::success;
  }
  /** 就绪项立即返回；缺失项进入同一加载组，回执包含全部请求的资源。 */
  engine::result<asset_load_completion>
  prepare_assets(std::span<const engine::render_resource_source> sources, std::uint64_t session,
                 std::uint64_t revision, bool reload, bool allow_load) try {
    using engine::error;
    if (stopping_)
      return error::invalid_state;
    if (sources.size() > 256 || !session || !revision)
      return error::invalid_argument;
    asset_load_completion ready;
    ready.session = session;
    ready.revision = revision;
    ready.profile = profile();
    ready.state = asset_load_state::applied;
    ready.result = error::success;
    std::vector<engine::render_resource_source> missing;
    std::set<std::string> unique;
    for (const auto& source : sources) {
      if (engine::validate_uri(source.uri).failed() || !unique.insert(source.uri).second ||
          (source.type != engine::render_resource_kind::mesh &&
           source.type != engine::render_resource_kind::material &&
           source.type != engine::render_resource_kind::texture))
        return error::invalid_argument;
      auto found = reload ? engine::result<engine::render_resource_ref>{error::not_found}
                          : service_.find(source, profile());
      if (found) {
        ready.assets.push_back(std::move(*found));
        ready.results.push_back({.source = source, .result = error::success, .applied = true});
      } else if (found.error() == error::not_found || found.error() == error::not_ready) {
        missing.push_back(source);
      } else
        return found.error();
    }
    if (missing.empty())
      return ready;
    if (!allow_load || busy())
      return error::not_ready;
    const auto cached_count = ready.results.size();
    for (const auto& source : missing)
      ready.results.push_back({.source = source});
    std::uint64_t request{};
    auto status = submit_assets(missing, session, revision, request, true);
    if (status.failed())
      return status;
    completion_->assets = std::move(ready.assets);
    completion_->results = std::move(ready.results);
    cursor_ = cached_count;
    ready.request = request;
    ready.state = asset_load_state::preparing;
    return ready;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
  void advance() {
    using engine::error;
    service_.advance();
    if (!completion_ || completion_->state != runtime::asset_load_state::preparing)
      return;
    auto& completion = *completion_;
    auto& item = completion.results[cursor_];
    if (!current_) {
      auto loaded = service_.load(item.source, reload_);
      if (!loaded)
        item.result = loaded.error();
      else
        current_ = std::move(*loaded);
    }
    if (current_) {
      item.result = current_.status();
      if (item.result == error::not_ready)
        return;
      const auto diagnostic = current_.diagnostic();
      if (item.result.failed()) {
        completion.message = diagnostic.message;
        completion.path = diagnostic.path;
        completion.byte_offset = diagnostic.byte_offset;
      }
      completion.prepared_bytes += current_.metrics().bytes;
      completion.prepare_ms += current_.metrics().decode_ms;
      completion.queue_ms += current_.metrics().queue_ms;
      if (item.result.ok())
        completion.assets.push_back(current_.resource());
      current_.cancel();
    }
    item.applied = item.result.ok();
    if (item.result.failed())
      completion.result = item.result;
    if (++cursor_ == completion.results.size())
      completion.state = completion.result.ok() ? runtime::asset_load_state::applied
                                                : runtime::asset_load_state::failed;
  }
  bool take(runtime::asset_load_completion& output) {
    if (!completion_ || completion_->state == runtime::asset_load_state::preparing)
      return false;
    completion_->commit_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started_)
            .count();
    output = std::move(*completion_);
    completion_.reset();
    return true;
  }
  bool owns(std::uint64_t request) const noexcept {
    return request != 0 && completion_ && completion_->request == request;
  }
  bool discard(std::uint64_t request) noexcept {
    if (!owns(request))
      return false;
    current_.cancel();
    completion_.reset();
    return true;
  }
  bool busy() const noexcept { return completion_.has_value(); }
  bool cancel(std::uint64_t request = 0) {
    if (request && !owns(request))
      return false;
    if (!completion_ || completion_->state != runtime::asset_load_state::preparing)
      return false;
    current_.cancel();
    completion_->state = runtime::asset_load_state::cancelled;
    completion_->result = engine::error::invalid_state;
    for (auto& item : completion_->results)
      if (item.result == engine::error::not_ready)
        item.result = engine::error::invalid_state;
    return true;
  }
  bool progress(runtime::asset_load_progress& output) const {
    if (!completion_ || completion_->state != runtime::asset_load_state::preparing)
      return false;
    output = {.request = completion_->request,
              .session = completion_->session,
              .revision = completion_->revision,
              .state = completion_->state,
              .completed_assets = cursor_,
              .total_assets = completion_->results.size(),
              .can_cancel = true};
    return true;
  }
  void request_stop() {
    stopping_ = true;
    (void)cancel();
  }
  bool stopped() const { return stopping_; }

private:
  engine::resource_service& service_;
  engine::resource_request current_;
  std::optional<runtime::asset_load_completion> completion_;
  std::chrono::steady_clock::time_point started_;
  std::uint64_t sequence_{};
  std::size_t cursor_{};
  bool reload_{}, stopping_{};
};
}
