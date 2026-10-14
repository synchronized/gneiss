// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

// 仅供内部实现使用。关闭时参数不求值，公共头文件不包含此文件。
#ifdef GNEISS_ENABLE_PROFILING
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace gneiss::diagnostics {
inline auto profile_task_label(std::uint64_t owner, std::uint64_t task) {
  std::array<char, 96> text{};
  std::snprintf(text.data(), text.size(), "scheduler=%llu task=%llu",
                static_cast<unsigned long long>(owner), static_cast<unsigned long long>(task));
  return text;
}
}
#define GNEISS_PROFILE_SCOPE(name) ZoneScopedN(name)
#define GNEISS_PROFILE_FRAME() FrameMark
#define GNEISS_PROFILE_THREAD(name) tracy::SetThreadName(name)
#define GNEISS_PROFILE_TEXT(value)                                                                 \
  do {                                                                                             \
    if (ZoneIsActive) {                                                                            \
      const std::string_view gneiss_profile_text{value};                                           \
      if (!gneiss_profile_text.empty()) {                                                          \
        ZoneText(gneiss_profile_text.data(),                                                       \
                 std::min(gneiss_profile_text.size(), std::size_t{1024}));                         \
      }                                                                                            \
    }                                                                                              \
  } while (false)
#define GNEISS_PROFILE_TASK(owner, id, name)                                                       \
  do {                                                                                             \
    if (ZoneIsActive) {                                                                            \
      const auto gneiss_profile_label = ::gneiss::diagnostics::profile_task_label(owner, id);      \
      ZoneText(gneiss_profile_label.data(),                                                        \
               std::char_traits<char>::length(gneiss_profile_label.data()));                       \
      if (!(name).empty()) {                                                                       \
        ZoneText((name).data(), std::min((name).size(), std::size_t{1024}));                       \
      }                                                                                            \
      ZoneValue(id);                                                                               \
    }                                                                                              \
  } while (false)
#else
#define GNEISS_PROFILE_SCOPE(name) ((void)0)
#define GNEISS_PROFILE_FRAME() ((void)0)
#define GNEISS_PROFILE_THREAD(name) ((void)0)
#define GNEISS_PROFILE_TEXT(value) ((void)0)
#define GNEISS_PROFILE_TASK(owner, id, name) ((void)0)
#endif
