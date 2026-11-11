// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "logging_runtime.hpp"
#include "engine/core/utf8.hpp"
#include "log_dispatcher.hpp"
#include <atomic>
#include <mutex>
#include <stdexcept>

namespace gneiss::engine::logging {
namespace {
thread_local bool in_output = false;
bool valid_level(level value) noexcept { return value >= level::trace && value <= level::fatal; }
struct output_scope {
  output_scope() {
    if (in_output)
      throw std::logic_error("recursive log output");
    in_output = true;
  }
  ~output_scope() { in_output = false; }
};
}
namespace detail {
struct runtime_state {
  std::atomic<level> threshold{level::info};
  std::atomic<bool> active{true};
  std::atomic<std::uint64_t> failures{};
  std::mutex mutex;
  std::mutex output_mutex;
  std::vector<std::unique_ptr<sink>> outputs;
  std::unique_ptr<log_dispatcher> dispatcher;
  void deliver(const event_view& event) {
    const std::scoped_lock lock(output_mutex);
    output_scope scope;
    for (const auto& output : outputs)
      if (output->write(event).failed())
        ++failures;
  }
  void flush_outputs() {
    const std::scoped_lock lock(output_mutex);
    output_scope scope;
    for (const auto& output : outputs)
      if (output->flush().failed())
        ++failures;
  }
};
}
bool logger::enabled(level severity) const noexcept {
  if (in_output || !valid_level(severity))
    return false;
  const auto state = state_.lock();
  return state && state->active.load() && severity >= state->threshold.load();
}
void logger::submit(level severity, std::string_view text) const {
  if (in_output)
    return;
  const auto state = state_.lock();
  if (!state)
    return;
  const std::scoped_lock lock(state->mutex);
  if (!state->dispatcher || severity < state->threshold.load())
    return;
  const auto status = state->dispatcher->submit({.severity = severity,
                                                 .source = channel_.view(),
                                                 .category = channel_.view(),
                                                 .message = text});
  if (status.failed())
    throw std::invalid_argument("invalid log message");
}
runtime::runtime(std::vector<std::unique_ptr<sink>> outputs, std::size_t capacity)
    : state_(std::make_shared<detail::runtime_state>()) {
  if (outputs.empty())
    throw std::invalid_argument("log outputs must not be empty");
  for (const auto& output : outputs)
    if (!output)
      throw std::invalid_argument("null log output");
  state_->outputs = std::move(outputs);
  state_->dispatcher = std::make_unique<log_dispatcher>(
      [state = state_.get()](const event_view& event) { state->deliver(event); }, capacity);
}
runtime::~runtime() { close(); }
logger runtime::get_logger(std::string_view channel) const {
  if (channel.empty() || !utf8::is_valid(channel))
    throw std::invalid_argument("invalid log channel");
  return {state_, string_name{channel}};
}
void runtime::set_level(level threshold) {
  if (!valid_level(threshold))
    throw std::invalid_argument("invalid log level");
  state_->threshold.store(threshold);
}
void runtime::flush() {
  if (in_output)
    throw std::logic_error("cannot flush from log output");
  // 宿主串行调用生命周期操作，不能并发调用 close；输出锁保护仍在提交的新消息。
  if (state_->dispatcher)
    state_->dispatcher->flush();
  state_->flush_outputs();
}
void runtime::close() {
  if (in_output)
    throw std::logic_error("cannot close from log output");
  std::unique_ptr<log_dispatcher> dispatcher;
  {
    const std::scoped_lock lock(state_->mutex);
    state_->active.store(false);
    dispatcher = std::move(state_->dispatcher);
  }
  if (dispatcher) {
    dispatcher.reset(); // 不持提交锁等待消费者排空并退出。
    state_->flush_outputs();
  }
}
std::uint64_t runtime::output_failures() const noexcept { return state_->failures.load(); }
}
