// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/application.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace gneiss::engine;
namespace {
struct capture_state {
  std::mutex mutex;
  std::condition_variable changed;
  std::uint64_t count{}, previous_sequence{};
  std::string source, category, message;
  error operation = error::success;
  error reentrant_result = error::success;
  gneiss::runtime::application* application{};
  bool check_reentrancy = true;
  std::atomic<bool> callback_active = false;
  std::atomic<bool> was_concurrent = false;
};
void capture(const event_view& event, capture_state& state) {
  if (state.callback_active.exchange(true))
    state.was_concurrent = true;
  {
    const std::scoped_lock lock(state.mutex);
    if (event.sequence > state.previous_sequence && event.timestamp_ns != 0 &&
        event.thread_id != 0) {
      state.previous_sequence = event.sequence;
      state.source = event.message.source;
      state.category = event.message.category;
      state.message = event.message.message;
      state.operation = event.message.result;
      ++state.count;
    }
    if (state.check_reentrancy && state.count == 1 && state.application) {
      state.reentrant_result = state.application->submit_log(
          {.severity = level::debug, .category = "test", .message = "nested"});
    }
  }
  state.callback_active = false;
  state.changed.notify_all();
}
}

int main() {
  // 接收状态先于 Application 构造，失败提前返回时也能安全排空日志。
  capture_state state, owned_capture, empty_capture;
  std::uint32_t throwing_calls{};
  gneiss::runtime::application_configuration config;
  config.callbacks.log = [&](const event_view& event) { capture(event, state); };
  std::shared_ptr<gneiss::runtime::application> application;
  if (gneiss::runtime::application::create(config, application).failed())
    return 1;
  state.application = application.get();
  std::string category = "game", text = "ready";
  if (application->submit_log({.source = "application", .category = category, .message = text})
          .failed())
    return 2;
  category.assign("changed");
  text.assign("changed");
  {
    std::unique_lock lock(state.mutex);
    if (!state.changed.wait_for(lock, std::chrono::seconds(2), [&] { return state.count >= 1; }))
      return 3;
    if (state.source != "application" || state.category != "game" || state.message != "ready" ||
        state.reentrant_result != error::invalid_state)
      return 4;
  }
  constexpr std::uint32_t thread_count = 4, messages_per_thread = 16;
  std::vector<std::thread> threads;
  for (std::uint32_t i = 0; i < thread_count; ++i) {
    threads.emplace_back([application] {
      for (std::uint32_t j = 0; j < messages_per_thread; ++j) {
        if (application
                ->submit_log({.severity = level::debug, .category = "worker", .message = "tick"})
                .failed())
          return;
      }
    });
  }
  for (auto& thread : threads)
    thread.join();
  const auto expected = UINT64_C(1) + thread_count * messages_per_thread;
  {
    std::unique_lock lock(state.mutex);
    if (!state.changed.wait_for(lock, std::chrono::seconds(2),
                                [&] { return state.count >= expected; }))
      return 5;
    if (state.count != expected || state.previous_sequence != expected || state.was_concurrent)
      return 6;
  }
  if (application->shutdown().failed())
    return 7;
  application.reset();

  // 无回调时仍执行消息校验；有效日志允许无接收方。
  config = {};
  if (gneiss::runtime::application::create(config, application).failed() ||
      application->submit_log({.category = "test", .message = "ready"}).failed() ||
      application->submit_log({}) != error::invalid_argument ||
      application->submit_log({.severity = static_cast<level>(0xffffffffU), .category = "test"}) !=
          error::invalid_argument ||
      application->submit_log({.category = "test", .message = "\xC0\x80"}) !=
          error::invalid_argument ||
      application->shutdown().failed())
    return 8;
  application.reset();

  // 接收方异常不穿出消费线程；销毁投递器时排空已接受消息。
  config.callbacks.log = [&](const event_view&) {
    ++throwing_calls;
    throw 1;
  };
  if (gneiss::runtime::application::create(config, application).failed() ||
      application->submit_log({.category = "test", .message = "throw"}).failed() ||
      application->submit_log({.category = "test", .message = "throw"}).failed() ||
      application->shutdown().failed())
    return 9;
  application.reset();
  if (throwing_calls != 2)
    return 9;

  owned_capture.check_reentrancy = false;
  config.callbacks.log = [&](const event_view& event) { capture(event, owned_capture); };
  if (gneiss::runtime::application::create(config, application).failed())
    return 10;
  error submitted;
  std::thread worker([&] {
    const std::string transient = "owned worker";
    submitted = application->submit_log({.severity = level::warning,
                                         .category = "cpp",
                                         .message = transient,
                                         .result = error::not_ready});
  });
  worker.join();
  if (submitted.failed() || application->shutdown().failed())
    return 11;
  application.reset();
  if (owned_capture.count != 1 || owned_capture.message != "owned worker" ||
      owned_capture.category != "cpp" || owned_capture.operation != error::not_ready ||
      owned_capture.callback_active || owned_capture.was_concurrent)
    return 12;

  empty_capture.check_reentrancy = false;
  config.callbacks.log = [&](const event_view& event) { capture(event, empty_capture); };
  if (gneiss::runtime::application::create(config, application).failed() ||
      application->submit_log({.source = "application", .category = "empty"}).failed() ||
      application->shutdown().failed())
    return 13;
  application.reset();
  if (empty_capture.count != 1 || !empty_capture.message.empty() ||
      empty_capture.category != "empty" || empty_capture.source != "application")
    return 14;
  return 0;
}
