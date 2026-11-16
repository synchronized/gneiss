// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/function/asset/asset_batch_request.hpp>
#include <gneiss/engine/function/asset/asset_batch_types.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/resource/asset_uri.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>
#include <set>
#include <thread>
namespace gneiss {
/** 场景/编辑器的请求汇总；不读文件、不解码、不创建或更新资源。 */
class resource_load_group final {
public:
  explicit resource_load_group(gneiss::asset_manager& service) : service_(service) {}
  ~resource_load_group() { *lifetime_ = nullptr; }
  gneiss::result<asset_batch_request> begin(std::span<const gneiss::render_resource_source> sources,
                                            bool reload) {
    using gneiss::error;
    if (stopping_)
      return error::invalid_state;
    if (busy())
      return error::not_ready;
    asset_load_completion next;
    next.profile = service_.profile();
    next.state = asset_load_state::preparing;
    next.result = error::success;
    std::set<std::string> unique;
    for (const auto& source : sources) {
      if (gneiss::validate_uri(source.uri).failed() || !unique.insert(source.uri).second ||
          (source.type != gneiss::render_resource_kind::mesh &&
           source.type != gneiss::render_resource_kind::texture &&
           source.type != gneiss::render_resource_kind::material))
        return error::invalid_argument;
      auto cached = reload ? gneiss::result<gneiss::render_resource_ref>{error::not_found}
                           : service_.find(source, next.profile);
      if (cached) {
        next.assets.push_back(std::move(*cached));
        next.results.push_back({.source = source, .result = error::success, .applied = true});
      } else {
        if (cached.error() != error::not_found && cached.error() != error::not_ready)
          return cached.error();
        next.results.push_back({.source = source});
      }
    }
    next.request = ++sequence_;
    completion_ = std::move(next);
    cursor_ = 0;
    reload_ = reload;
    started_ = std::chrono::steady_clock::now();
    advance();
    return asset_batch_request{lifetime_, completion_->request};
  }
  /** 只推进批次观察状态；宿主先推进共享资产管理器。 */
  void advance() {
    using gneiss::error;
    if (!completion_ || completion_->state != gneiss::asset_load_state::preparing)
      return;
    auto& completion = *completion_;
    while (cursor_ < completion.results.size() && completion.results[cursor_].applied)
      ++cursor_;
    if (cursor_ == completion.results.size()) {
      completion.state =
          completion.result.ok() ? asset_load_state::completed : asset_load_state::failed;
      return;
    }
    auto& item = completion.results[cursor_];
    if (!current_) {
      auto loaded = service_.load(item.source, {.reload = reload_});
      if (!loaded)
        item.result = loaded.error();
      else {
        current_ = std::move(*loaded);
        ready_ = std::make_shared<std::atomic<bool>>(false);
        gneiss::detail::asset_request_access::when_ready(current_, [ready = std::weak_ptr{ready_}] {
          if (auto receipt = ready.lock())
            receipt->store(true);
        });
      }
    }
    if (current_) {
      if (!ready_->load())
        return;
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
      ready_.reset();
      current_.cancel();
    }
    item.applied = item.result.ok();
    if (item.result.failed())
      completion.result = item.result;
    if (++cursor_ == completion.results.size())
      completion.state = completion.result.ok() ? gneiss::asset_load_state::completed
                                                : gneiss::asset_load_state::failed;
  }
  bool take(gneiss::asset_load_completion& output) {
    if (!completion_ || completion_->state == gneiss::asset_load_state::preparing)
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
    ready_.reset();
    current_.cancel();
    completion_.reset();
    return true;
  }
  bool busy() const noexcept { return completion_.has_value(); }
  bool cancel(std::uint64_t request = 0) {
    if (request && !owns(request))
      return false;
    if (!completion_ || completion_->state != gneiss::asset_load_state::preparing)
      return false;
    ready_.reset();
    current_.cancel();
    completion_->state = gneiss::asset_load_state::cancelled;
    completion_->result = gneiss::error::invalid_state;
    for (auto& item : completion_->results)
      if (item.result == gneiss::error::not_ready)
        item.result = gneiss::error::invalid_state;
    return true;
  }
  bool progress(gneiss::asset_load_progress& output) const {
    if (!completion_)
      return false;
    output = {.request = completion_->request,
              .state = completion_->state,
              .completed_assets = cursor_,
              .total_assets = completion_->results.size(),
              .can_cancel = completion_->state == asset_load_state::preparing};
    return true;
  }
  void request_stop() {
    stopping_ = true;
    (void)cancel();
  }
  bool stopped() const { return stopping_; }

private:
  friend class asset_batch_request;
  const std::thread::id owner_{std::this_thread::get_id()};
  std::shared_ptr<resource_load_group*> lifetime_{std::make_shared<resource_load_group*>(this)};
  gneiss::asset_manager& service_;
  gneiss::asset_request<gneiss::render_resource_ref> current_;
  std::optional<gneiss::asset_load_completion> completion_;
  std::chrono::steady_clock::time_point started_;
  std::uint64_t sequence_{};
  std::size_t cursor_{};
  std::shared_ptr<std::atomic<bool>> ready_;
  bool reload_{}, stopping_{};
};
}
