// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/utf8.hpp"
#include <array>
#include <string_view>
int main() {
  using gneiss::engine::utf8::is_valid;
  using namespace std::string_view_literals;
  const std::array valid{""sv,
                         "ASCII"sv,
                         "\0"sv,
                         "\xc2\x80"sv,
                         "\xdf\xbf"sv,
                         "\xe0\xa0\x80"sv,
                         "\xed\x9f\xbf"sv,
                         "\xee\x80\x80"sv,
                         "\xef\xbf\xbf"sv,
                         "\xf0\x90\x80\x80"sv,
                         "\xf4\x8f\xbf\xbf"sv,
                         "中文"sv};
  for (auto value : valid)
    if (!is_valid(value))
      return 1;
  const std::array invalid{"\x80"sv,
                           "\xc0\x80"sv,
                           "\xc1\xbf"sv,
                           "\xc2"sv,
                           "\xe0\x9f\xbf"sv,
                           "\xed\xa0\x80"sv,
                           "\xed\xbf\xbf"sv,
                           "\xe1\x80"sv,
                           "\xf0\x8f\xbf\xbf"sv,
                           "\xf4\x90\x80\x80"sv,
                           "\xf5\x80\x80\x80"sv,
                           "\xf0\x90\x80"sv,
                           "\xff"sv,
                           "\xe1!\x80"sv};
  for (auto value : invalid)
    if (is_valid(value))
      return 2;
  return 0;
}
