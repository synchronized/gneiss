// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.hpp>
#include <gneiss/log.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

struct capture_state final {
  std::mutex mutex;
  std::condition_variable changed;
  std::uint64_t count = 0U;
  std::uint64_t previous_sequence = 0U;
  std::string source;
  std::string category;
  std::string message;
  gneiss::result operation = gneiss::result::success;
  gneiss_result reentrant_result = GNEISS_SUCCESS;
  bool check_reentrancy = true;
  std::atomic<bool> callback_active = false;
  std::atomic<bool> was_concurrent = false;
};

void capture(gneiss_application application, const gneiss_log_event* event, void* user_data) {
  auto& state = *static_cast<capture_state*>(user_data);
  if (state.callback_active.exchange(true)) {
    state.was_concurrent = true;
  }
  {
    const std::scoped_lock lock(state.mutex);
    if (event != nullptr && event->struct_size >= GNEISS_LOG_EVENT_VERSION_1_SIZE &&
        event->sequence > state.previous_sequence && event->timestamp_ns != 0U &&
        event->thread_id != 0U && event->flags == 0U) {
      state.previous_sequence = event->sequence;
      state.source.assign(event->source, event->source_length);
      state.category.assign(event->category, event->category_length);
      state.message.assign(event->message, event->message_length);
      state.operation = gneiss::from_native(event->result);
      ++state.count;
    }
    if (state.check_reentrancy && state.count == 1U) {
      const auto nested = gneiss::to_native(
          gneiss::make_log_message(gneiss::log_severity::debug, "test", "nested"));
      state.reentrant_result = gneiss_application_log(application, &nested);
    }
  }
  state.callback_active = false;
  state.changed.notify_all();
}

void throwing_capture(gneiss_application, const gneiss_log_event*, void* user_data) {
  ++*static_cast<std::uint32_t*>(user_data);
  throw 1;
}

} // namespace

int main() {
  capture_state state;
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &state;
  desc.log = capture;
  gneiss_application application = GNEISS_NULL_APPLICATION;
  if (gneiss_application_create(&desc, &application) != GNEISS_SUCCESS) {
    return 1;
  }

  std::string category = "game";
  std::string text = "ready";
  auto first =
      gneiss::to_native(gneiss::make_log_message(gneiss::log_severity::info, category, text));
  if (gneiss_application_log(application, &first) != GNEISS_SUCCESS) {
    return 2;
  }
  category.assign("changed");
  text.assign("changed");
  {
    std::unique_lock lock(state.mutex);
    if (!state.changed.wait_for(lock, std::chrono::seconds(2),
                                [&state] { return state.count >= 1U; })) {
      return 3;
    }
    if (state.source != "application" || state.category != "game" || state.message != "ready" ||
        state.reentrant_result != GNEISS_ERROR_INVALID_STATE) {
      return 4;
    }
  }

  constexpr std::uint32_t thread_count = 4U;
  constexpr std::uint32_t messages_per_thread = 16U;
  std::vector<std::thread> threads;
  for (std::uint32_t thread_index = 0U; thread_index < thread_count; ++thread_index) {
    threads.emplace_back([application] {
      const auto message = gneiss::to_native(
          gneiss::make_log_message(gneiss::log_severity::debug, "worker", "tick"));
      for (std::uint32_t index = 0U; index < messages_per_thread; ++index) {
        if (gneiss_application_log(application, &message) != GNEISS_SUCCESS) {
          return;
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  const auto expected = UINT64_C(1) + thread_count * messages_per_thread;
  {
    std::unique_lock lock(state.mutex);
    if (!state.changed.wait_for(lock, std::chrono::seconds(2),
                                [&state] { return state.count >= expected; })) {
      return 5;
    }
    if (state.count != expected || state.previous_sequence != expected || state.was_concurrent) {
      return 6;
    }
  }
  if (gneiss_application_destroy(application) != GNEISS_SUCCESS ||
      gneiss_application_log(application, &first) != GNEISS_ERROR_INVALID_HANDLE) {
    return 7;
  }

  desc = GNEISS_APPLICATION_DESC_INIT;
  if (gneiss_application_create(&desc, &application) != GNEISS_SUCCESS ||
      gneiss_application_log(application, &first) != GNEISS_SUCCESS ||
      gneiss_application_destroy(application) != GNEISS_SUCCESS) {
    return 8;
  }
  // Application 适配器不得提前 noexcept 终止，关闭时必须等待两个事件处理完。
  std::uint32_t throwing_calls{};
  desc.log = throwing_capture;
  desc.user_data = &throwing_calls;
  const auto throwing_message =
      gneiss::to_native(gneiss::make_log_message(gneiss::log_severity::info, "test", "throw"));
  if (gneiss_application_create(&desc, &application) != GNEISS_SUCCESS ||
      gneiss_application_log(application, &throwing_message) != GNEISS_SUCCESS ||
      gneiss_application_log(application, &throwing_message) != GNEISS_SUCCESS ||
      gneiss_application_destroy(application) != GNEISS_SUCCESS || throwing_calls != 2U) {
    return 9;
  }
  // C++ 拥有者允许工作线程提交日志，关闭等待回调结束；不并发修改拥有者本身。
  capture_state owned_capture;
  // 关闭排空期间句柄可先失效；重入拒绝已在上面的存活期用例验证。
  owned_capture.check_reentrancy = false;
  desc.log = capture;
  desc.user_data = &owned_capture;
  gneiss::application owned;
  if (gneiss::application::create(desc, owned).failed()) {
    return 10;
  }
  gneiss::result submitted;
  std::thread worker([&] {
    const std::string transient = "owned worker";
    submitted = owned.log({.severity = gneiss::log_severity::warning,
                           .category = "cpp",
                           .message = transient,
                           .operation = gneiss::result::not_ready});
  });
  worker.join();
  if (submitted.failed() || owned.reset().failed()) {
    return 11;
  }
  const std::scoped_lock lock(owned_capture.mutex);
  if (owned_capture.count != 1U || owned_capture.message != "owned worker" ||
      owned_capture.category != "cpp" || owned_capture.operation != gneiss::result::not_ready ||
      owned_capture.callback_active || owned_capture.was_concurrent) {
    return 12;
  }
  capture_state empty_capture;
  empty_capture.check_reentrancy = false;
  desc.user_data = &empty_capture;
  if (gneiss::application::create(desc, owned).failed() ||
      owned.log(gneiss::make_log_message(gneiss::log_severity::info, "empty", {})).failed() ||
      owned.reset().failed()) {
    return 13;
  }
  const std::scoped_lock empty_lock(empty_capture.mutex);
  if (empty_capture.count != 1U || !empty_capture.message.empty() ||
      empty_capture.category != "empty" || empty_capture.source != "application") {
    return 14;
  }
  return 0;
}
