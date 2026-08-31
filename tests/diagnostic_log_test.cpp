// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

namespace {

struct capture_state final {
  std::uint32_t diagnostic_count = 0U;
  std::uint32_t log_count = 0U;
  std::mutex mutex;
  std::condition_variable changed;
  std::string source;
  std::string category;
  gneiss_result result = GNEISS_SUCCESS;
};

gneiss_result fail_update(gneiss_application, const gneiss_frame_time*, void*) {
  return GNEISS_ERROR_INTERNAL;
}

void capture_diagnostic(gneiss_application, const gneiss_diagnostic* diagnostic, void* user_data) {
  auto& state = *static_cast<capture_state*>(user_data);
  if (diagnostic != nullptr && diagnostic->result == GNEISS_ERROR_INTERNAL) {
    ++state.diagnostic_count;
  }
}

void capture_log(gneiss_application, const gneiss_log_event* event, void* user_data) {
  auto& state = *static_cast<capture_state*>(user_data);
  if (event == nullptr) {
    return;
  }
  {
    const std::scoped_lock lock(state.mutex);
    ++state.log_count;
    state.source.assign(event->source, event->source_length);
    state.category.assign(event->category, event->category_length);
    state.result = event->result;
  }
  state.changed.notify_all();
}

} // namespace

int main() {
  capture_state state;
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &state;
  desc.update = fail_update;
  desc.diagnostic = capture_diagnostic;
  desc.log = capture_log;
  gneiss_application application = GNEISS_NULL_APPLICATION;
  if (gneiss_application_create(&desc, &application) != GNEISS_SUCCESS ||
      gneiss_application_run(application, 1U) != GNEISS_ERROR_INTERNAL ||
      state.diagnostic_count != 1U) {
    return 1;
  }
  {
    std::unique_lock lock(state.mutex);
    if (!state.changed.wait_for(lock, std::chrono::seconds(2),
                                [&state] { return state.log_count == 1U; }) ||
        state.source != "application" || state.category != "backend" ||
        state.result != GNEISS_ERROR_INTERNAL) {
      return 2;
    }
  }
  if (gneiss_application_destroy(application) != GNEISS_SUCCESS) {
    return 3;
  }
  return 0;
}
