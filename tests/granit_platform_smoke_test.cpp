// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.hpp>

#include <string_view>

int main() {
  constexpr std::string_view title = "Gneiss Granit Platform Smoke Test";
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_title = title.data();
  desc.window_title_length = static_cast<std::uint32_t>(title.size());
  desc.window_width = 320;
  desc.window_height = 240;

  gneiss::application application;
  const auto create_result = gneiss::application::create(desc, application);
  if (create_result != gneiss::result::success) {
    return 1;
  }
  return application.run(3) == gneiss::result::success ? 0 : 2;
}
