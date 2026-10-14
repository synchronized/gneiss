// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/diagnostics/profiling.hpp"

// 本目标不接入 profiling。关闭宏必须不求值，也不要求参数引用的对象存在。
int main() {
  int evaluated{};
  GNEISS_PROFILE_SCOPE(++evaluated);
  GNEISS_PROFILE_THREAD(++evaluated);
  GNEISS_PROFILE_TEXT(++evaluated);
  GNEISS_PROFILE_TEXT(unavailable_text);
  GNEISS_PROFILE_TASK(++evaluated, unavailable_task, unavailable_name);
  GNEISS_PROFILE_FRAME();
  return evaluated;
}
