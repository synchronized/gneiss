// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/core/logging/service.hpp>
#include <gneiss/engine/core/utf8.hpp>

#include "detail/service_state.hpp"
#include <stdexcept>
namespace gneiss::logging {
using detail::check_output;
using detail::valid_level;
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
  GNEISS_ASSERT(state_->active, "不能向已关闭日志服务添加输出");
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
}
