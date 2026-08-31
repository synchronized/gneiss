// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.h>
#include <gneiss/log.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

struct capture_state final {
  std::mutex mutex;
  std::uint64_t count = 0U;
  std::uint64_t previous_sequence = 0U;
  std::string source;
  std::string category;
  std::string message;
  gneiss_result reentrant_result = GNEISS_SUCCESS;
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
      ++state.count;
    }
    if (state.count == 1U) {
      const auto nested = gneiss::make_log_message(gneiss::log_severity::debug, "test", "nested");
      state.reentrant_result = gneiss_application_log(application, &nested);
    }
  }
  state.callback_active = false;
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
  auto first = gneiss::make_log_message(gneiss::log_severity::info, category, text);
  if (gneiss_application_log(application, &first) != GNEISS_SUCCESS) {
    return 2;
  }
  category.assign("changed");
  text.assign("changed");
  {
    const std::scoped_lock lock(state.mutex);
    if (state.source != "application" || state.category != "game" || state.message != "ready" ||
        state.reentrant_result != GNEISS_ERROR_INVALID_STATE) {
      return 3;
    }
  }

  constexpr std::uint32_t thread_count = 4U;
  constexpr std::uint32_t messages_per_thread = 16U;
  std::vector<std::thread> threads;
  for (std::uint32_t thread_index = 0U; thread_index < thread_count; ++thread_index) {
    threads.emplace_back([application] {
      const auto message = gneiss::make_log_message(gneiss::log_severity::debug, "worker", "tick");
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
    const std::scoped_lock lock(state.mutex);
    if (state.count != expected || state.previous_sequence != expected || state.was_concurrent) {
      return 4;
    }
  }
  if (gneiss_application_destroy(application) != GNEISS_SUCCESS ||
      gneiss_application_log(application, &first) != GNEISS_ERROR_INVALID_HANDLE) {
    return 5;
  }

  desc = GNEISS_APPLICATION_DESC_INIT;
  if (gneiss_application_create(&desc, &application) != GNEISS_SUCCESS ||
      gneiss_application_log(application, &first) != GNEISS_SUCCESS ||
      gneiss_application_destroy(application) != GNEISS_SUCCESS) {
    return 6;
  }
  return 0;
}
