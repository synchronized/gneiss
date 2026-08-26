// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.hpp>

namespace {

struct context {
  std::uint64_t frame_count = 0;
};

gneiss_result update(gneiss_application application, const gneiss_frame_time* time,
                     void* user_data) {
  auto& state = *static_cast<context*>(user_data);
  ++state.frame_count;
  if (time->frame_index == 0U) {
    const auto result = gneiss_application_set_paused(application, UINT8_C(1));
    if (result != GNEISS_SUCCESS) {
      return result;
    }
  } else if (time->delta_ns != 0U || time->is_paused == 0U) {
    return GNEISS_ERROR_INTERNAL;
  }
  return GNEISS_SUCCESS;
}

} // namespace

int main() {
  context state;
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &state;
  desc.update = update;
  gneiss::application application;
  if (gneiss::application::create(desc, application) != gneiss::result::success ||
      application.run(2) != gneiss::result::success ||
      application.run(1) != gneiss::result::success || state.frame_count != 3U) {
    return 1;
  }
  return 0;
}
