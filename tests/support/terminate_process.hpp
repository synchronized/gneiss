// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <cstdlib>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace gneiss::test {
/** 死亡测试已进入 terminate handler 后立即结束进程，不执行 DLL 卸载回调。
 * 测试只验收错误线程析构触发终止，不在该线程再次清理其他存活资源。 */
[[noreturn]] inline void terminate_test_process(unsigned exit_code) noexcept {
#ifdef _WIN32
  (void)TerminateProcess(GetCurrentProcess(), exit_code);
  std::abort();
#else
  std::_Exit(static_cast<int>(exit_code));
#endif
}
}
