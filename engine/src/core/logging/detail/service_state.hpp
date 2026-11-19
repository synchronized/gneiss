// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_queue.hpp"
#include <algorithm>
#include <atomic>
#include <gneiss/engine/core/base/assert.hpp>
#include <gneiss/engine/core/logging/service.hpp>
#include <mutex>
#include <vector>
namespace gneiss::logging::detail {
inline thread_local bool in_output{};
inline bool valid_level(level value) { return value >= level::trace && value <= level::fatal; }
inline void check_output() { GNEISS_ASSERT(!in_output, "不能在日志输出中执行生命周期操作"); }
struct output_scope {
  output_scope() {
    check_output();
    in_output = true;
  }
  ~output_scope() { in_output = false; }
};
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
std::shared_ptr<service_state> resolve_default_service();
}
