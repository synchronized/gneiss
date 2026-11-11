// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "console_model.hpp"
#include "engine/core/logging/sink.hpp"
#include <deque>
#include <memory>
#include <mutex>
namespace gneiss::host {
/** 日志线程到 UI 的有界交接缓冲，不启动线程，不在 Sink 中访问模型。 */
class console_log_buffer final {
  struct state {
    std::mutex mutex;
    std::deque<editor::console_event> pending;
    std::size_t dropped{};
  };
  class output final : public logging::sink {
  public:
    explicit output(std::shared_ptr<state> state) : state_(std::move(state)) {}
    error write(const logging::record_view& value) override {
      editor::console_event event{.severity = static_cast<std::uint32_t>(value.message.severity),
                                  .sequence = value.sequence,
                                  .timestamp_ns = value.timestamp_ns,
                                  .thread_id = value.thread_id,
                                  .context = value.message.context,
                                  .source = std::string(value.message.source),
                                  .category = std::string(value.message.category),
                                  .message = std::string(value.message.message),
                                  .operation = value.message.result};
      std::lock_guard lock(state_->mutex);
      if (state_->pending.size() == capacity) {
        state_->pending.pop_front();
        ++state_->dropped;
      }
      state_->pending.push_back(std::move(event));
      return error::success;
    }
    error flush() override { return error::success; }

  private:
    std::shared_ptr<state> state_;
  };

public:
  static constexpr std::size_t capacity = 1024;
  std::unique_ptr<logging::sink> make_sink() { return std::make_unique<output>(state_); }
  /** 主线程排空。先解锁再发布模型通知，回调不会在缓冲锁下运行。 */
  void drain(editor::console_model& target) {
    std::deque<editor::console_event> pending;
    std::size_t dropped;
    {
      std::lock_guard lock(state_->mutex);
      pending.swap(state_->pending);
      dropped = std::exchange(state_->dropped, 0);
    }
    if (dropped) {
      (void)target.append_local_event(
          {.severity = static_cast<std::uint32_t>(logging::level::warning),
           .source = "editor",
           .category = "console",
           .message = "本地日志缓冲丢弃 " + std::to_string(dropped) + " 条记录"});
    }
    for (auto& event : pending)
      (void)target.append_local_event(std::move(event));
  }

private:
  std::shared_ptr<state> state_ = std::make_shared<state>();
};
}
