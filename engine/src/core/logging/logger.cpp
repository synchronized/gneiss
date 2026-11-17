// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <chrono>
#include <gneiss/engine/core/logging/logger.hpp>
#include <gneiss/engine/core/utf8.hpp>
#include <thread>

#include "detail/service_state.hpp"
#include <stdexcept>
namespace gneiss::logging {
using detail::in_output;
using detail::output_scope;
using detail::valid_level;
logger::logger(std::string_view channel) : use_default_(true), channel_(channel) {
  if (channel.empty() || !utf8::is_valid(channel))
    throw std::invalid_argument("invalid log channel");
}
std::shared_ptr<detail::service_state> logger::resolve() const {
  if (!use_default_)
    return state_.lock();
  return detail::resolve_default_service();
}
bool logger::enabled(level severity) const {
  if (in_output || !valid_level(severity))
    return false;
  auto state = resolve();
  return state && state->enabled(severity);
}
void logger::submit(level severity, std::string_view text) const {
  write({.severity = severity, .message = text});
}
void logger::write(message_view message) const {
  if (in_output || !valid_level(message.severity))
    return;
  auto state = resolve();
  if (!state || !state->enabled(message.severity))
    return;
  const auto channel = channel_.view();
  if (message.source.empty())
    message.source = channel;
  if (message.category.empty())
    message.category = channel;
  const auto severity = message.severity;
  std::unique_lock lock(state->mutex);
  if (!state->active || severity < state->threshold)
    return;
  if (state->queue) {
    const auto submitted = state->queue->submit(message);
    GNEISS_ASSERT(!submitted.failed(), "日志队列意外拒绝内部提交");
    return;
  }
  lock.unlock();
  std::scoped_lock delivery(state->output_mutex);
  // 同步输出串行化；在输出锁内再次确认关闭状态。
  {
    std::scoped_lock gate(state->mutex);
    if (!state->active)
      return;
  }
  output_scope scope;
  auto targets = state->snapshot(severity);
  const auto timestamp = std::chrono::steady_clock::now().time_since_epoch();
  record_view record{
      message, ++state->sequence,
      static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(timestamp).count()),
      static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()))};
  for (auto& target : targets)
    if (target->output->write(record).failed())
      ++state->failures;
}
}
