// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#if defined(GNEISS_TEST_RELEASE_ASSERT)
#ifndef NDEBUG
#define NDEBUG
#endif
#else
#undef NDEBUG
#endif
#include "engine/core/assert.hpp"
#include <cstdio>
#include <cstdlib>
#include <string_view>

int main(int argc, char** argv) {
  // 子进程使用确定的终止码，避免默认 abort 弹窗；仍验证 libassert 的表达式诊断。
  libassert::set_failure_handler([](const libassert::assertion_info& info) {
    const auto text = info.to_string();
    std::fputs(text.c_str(), stderr);
    std::fflush(stderr);
    std::_Exit(86);
  });
  if (argc != 2)
    return 2;
  const std::string_view mode{argv[1]};
  int count = 0;
  if (mode == "pass") {
    GNEISS_ASSERT(++count == 1);
    GNEISS_DEBUG_ASSERT(++count == 2);
#ifdef NDEBUG
    return count == 1 ? 0 : 3;
#else
    return count == 2 ? 0 : 3;
#endif
  }
  if (mode == "assert")
    GNEISS_ASSERT(count == 42, "gneiss_assert_probe", count);
  else if (mode == "debug")
    GNEISS_DEBUG_ASSERT(count == 42, "gneiss_assert_probe", count);
  else if (mode == "panic")
    GNEISS_PANIC("gneiss_assert_probe");
  else
    return 4;
  return 0;
}
