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
  engine::result
  submit_assets(std::span<const engine::asset_source> sources, std::uint64_t session,
                std::uint64_t revision, std::uint64_t& request, bool reload = true,
                std::size_t limit = engine::resource_service::maximum_prepared_bytes) {
    using engine::result;
    request = 0;
    if (stopping_)
      return result::invalid_state;
    if (busy())
      return result::not_ready;
    if (sources.empty() || sources.size() > 256 || session == 0 || revision == 0 || limit == 0)
      return result::invalid_argument;
    std::set<std::string> unique;
    for (const auto& source : sources)
      if (engine::validate_uri(source.uri).failed() || !unique.insert(source.uri).second ||
          (source.type != engine::asset_type::mesh && source.type != engine::asset_type::texture &&
           source.type != engine::asset_type::material))
        return result::invalid_argument;
    runtime::asset_load_completion next;
    for (const auto& source : sources)
      next.results.push_back({.source = source});
    next.request = ++sequence_;
    next.session = session;
    next.revision = revision;
    next.profile = profile();
    next.result = result::success;
    next.state = runtime::asset_load_state::preparing;
    completion_ = std::move(next);
    cursor_ = 0;
    reload_ = reload;
    limit_ = limit;
    started_ = std::chrono::steady_clock::now();
    request = completion_->request;
    return result::success;
  }
  void advance() {
    using engine::result;
    service_.advance();
    if (!completion_ || completion_->state != runtime::asset_load_state::preparing)
      return;
    auto& completion = *completion_;
    auto& item = completion.results[cursor_];
    if (!current_) {
      const auto status = service_.load(item.source, current_, reload_,
                                        std::min(limit_, std::size_t{64U * 1024U * 1024U}));
      if (status.failed())
        item.result = status;
    }
    if (current_) {
      item.result = current_.status();
      if (item.result == result::not_ready)
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
  bool busy() const noexcept { return completion_.has_value(); }
  bool cancel() {
    if (!completion_ || completion_->state != runtime::asset_load_state::preparing)
      return false;
    current_.cancel();
    completion_->state = runtime::asset_load_state::cancelled;
    completion_->result = engine::result::invalid_state;
    for (auto& item : completion_->results)
      if (item.result == engine::result::not_ready)
        item.result = engine::result::invalid_state;
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
  std::size_t cursor_{}, limit_{};
  bool reload_{}, stopping_{};
};
}
