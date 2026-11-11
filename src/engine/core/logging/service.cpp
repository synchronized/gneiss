// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "service.hpp"
#include "default_service.hpp"
#include "detail/async_queue.hpp"
#include "engine/core/utf8.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
namespace gneiss::engine::logging {
namespace {
thread_local bool in_output{};
bool valid_level(level value) { return value >= level::trace && value <= level::fatal; }
void check_output() {
  if (in_output)
    throw std::logic_error("operation forbidden in log output");
}
struct output_scope {
  output_scope() {
    check_output();
    in_output = true;
  }
  ~output_scope() { in_output = false; }
};
}
namespace detail {
struct sink_slot {
  std::unique_ptr<sink> output;
  level threshold{level::trace};
  bool attached{true};
};
struct service_state {
  std::atomic<std::uint64_t> failures{};
  std::mutex mutex, output_mutex;
  bool active{true};
  level threshold{level::info};
  std::uint64_t sequence{};
  std::vector<std::shared_ptr<sink_slot>> outputs;
  std::unique_ptr<async_queue> queue;
  bool enabled(level severity) {
    std::scoped_lock lock(mutex);
    return active && severity >= threshold &&
           std::ranges::any_of(outputs, [severity](const auto& slot) {
             return slot->attached && severity >= slot->threshold;
           });
  }
  std::vector<std::shared_ptr<sink_slot>> snapshot(level severity, bool all = false) {
    std::scoped_lock lock(mutex);
    std::vector<std::shared_ptr<sink_slot>> result;
    for (auto& slot : outputs)
      if (slot->attached && (all || severity >= slot->threshold))
        result.push_back(slot);
    return result;
  }
  void deliver(const record_view& event) {
    std::scoped_lock delivery(output_mutex);
    output_scope scope;
    auto targets = snapshot(event.message.severity);
    for (auto& target : targets)
      if (target->output->write(event).failed())
        ++failures;
  }
  void flush_outputs() {
    std::scoped_lock delivery(output_mutex);
    output_scope scope;
    auto targets = snapshot(level::trace, true);
    for (auto& target : targets)
      if (target->output->flush().failed())
        ++failures;
  }
};
}
namespace {
struct default_route {
  std::mutex mutex;
  std::weak_ptr<detail::service_state> state;
};
default_route& route() {
  // 进程生命周期的小型路由槽，避免静态析构期间 Logger 访问已销毁的互斥量；不保活服务。
  static auto* value = new default_route;
  return *value;
}
}
std::shared_ptr<detail::service_state> logger::resolve() const {
  if (!use_default_)
    return state_.lock();
  auto& value = route();
  std::scoped_lock lock(value.mutex);
  return value.state.lock();
}
bool logger::enabled(level severity) const {
  if (in_output || !valid_level(severity))
    return false;
  auto state = resolve();
  return state && state->enabled(severity);
}
void logger::submit(level severity, std::string_view text) const {
  if (in_output)
    return;
  auto state = resolve();
  if (!state)
    return;
  const auto channel = use_default_ ? default_channel_ : channel_.view();
  const message_view message{
      .severity = severity, .source = channel, .category = channel, .message = text};
  std::unique_lock lock(state->mutex);
  if (!state->active || severity < state->threshold)
    return;
  if (state->queue) {
    if (state->queue->submit(message).failed())
      throw std::logic_error("log queue rejected submission");
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
service::service(service_options options) : state_(std::make_shared<detail::service_state>()) {
  if (!valid_level(options.minimum_level) || options.queue_capacity == 0 ||
      (options.mode != dispatch_mode::synchronous && options.mode != dispatch_mode::asynchronous))
    throw std::invalid_argument("invalid log service options");
  state_->threshold = options.minimum_level;
  if (options.mode == dispatch_mode::asynchronous)
    state_->queue = std::make_unique<detail::async_queue>(
        [state = state_.get()](const record_view& value) { state->deliver(value); },
        options.queue_capacity);
}
service::~service() { close(); }
logger service::get_logger(std::string_view channel) const {
  if (channel.empty() || !utf8::is_valid(channel))
    throw std::invalid_argument("invalid log channel");
  return {state_, string_name{channel}};
}
sink_connection service::attach(std::unique_ptr<sink> output, sink_options options) {
  if (!output || !valid_level(options.minimum_level))
    throw std::invalid_argument("invalid log sink");
  auto slot = std::make_shared<detail::sink_slot>();
  slot->output = std::move(output);
  slot->threshold = options.minimum_level;
  std::scoped_lock lock(state_->mutex);
  if (!state_->active)
    throw std::logic_error("log service closed");
  state_->outputs.push_back(slot);
  return {state_, slot};
}
void service::set_level(level value) {
  if (!valid_level(value))
    throw std::invalid_argument("invalid log level");
  std::scoped_lock lock(state_->mutex);
  state_->threshold = value;
}
void service::flush() {
  check_output();
  if (state_->queue)
    state_->queue->flush();
  state_->flush_outputs();
}
void service::close() {
  check_output();
  std::unique_ptr<detail::async_queue> queue;
  {
    std::scoped_lock lock(state_->mutex);
    if (!state_->active)
      return;
    state_->active = false;
    queue = std::move(state_->queue);
  }
  queue.reset();
  state_->flush_outputs();
  std::vector<std::shared_ptr<detail::sink_slot>> retired;
  {
    std::scoped_lock lock(state_->mutex);
    for (auto& slot : state_->outputs)
      slot->attached = false;
    retired.swap(state_->outputs);
  }
}
service_metrics service::metrics() const noexcept { return {state_->failures.load()}; }
void sink_connection::disconnect() const {
  auto state = state_.lock();
  auto slot = slot_.lock();
  if (!state || !slot)
    return;
  {
    std::scoped_lock lock(state->mutex);
    slot->attached = false;
    std::erase(state->outputs, slot);
  }
}
bool sink_connection::connected() const {
  auto state = state_.lock();
  auto slot = slot_.lock();
  if (!state || !slot)
    return false;
  std::scoped_lock lock(state->mutex);
  return state->active && slot->attached;
}
void sink_connection::set_level(level value) const {
  if (!valid_level(value))
    throw std::invalid_argument("invalid log level");
  auto state = state_.lock();
  auto slot = slot_.lock();
  if (!state || !slot)
    return;
  std::scoped_lock lock(state->mutex);
  slot->threshold = value;
}
default_service_binding::default_service_binding(service& logs) {
  auto& value = route();
  std::scoped_lock lock(value.mutex);
  if (!value.state.expired())
    throw std::logic_error("default log service already installed");
  {
    std::scoped_lock gate(logs.state_->mutex);
    if (!logs.state_->active)
      throw std::logic_error("log service closed");
  }
  state_ = logs.state_;
  value.state = state_;
}
default_service_binding::~default_service_binding() { reset(); }
void default_service_binding::reset() noexcept {
  auto& value = route();
  std::scoped_lock lock(value.mutex);
  if (!state_.owner_before(value.state) && !value.state.owner_before(state_))
    value.state.reset();
  state_.reset();
}
}
