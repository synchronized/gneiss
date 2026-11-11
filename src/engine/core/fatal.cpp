// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "fatal.hpp"
#include <cstdlib>
#include <exception>
#include <new>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace gneiss {
[[noreturn]] void fatal(fatal_reason reason) noexcept {
  // 直接写标准错误，不经过可能分配内存或持有业务锁的日志系统。
  constexpr char oom[] = "Gneiss fatal: out of memory\n";
  constexpr char unexpected[] = "Gneiss fatal: unhandled exception\n";
  const char* message = reason == fatal_reason::out_of_memory ? oom : unexpected;
  const auto size =
      reason == fatal_reason::out_of_memory ? sizeof(oom) - 1 : sizeof(unexpected) - 1;
#ifdef _WIN32
  (void)_write(2, message, static_cast<unsigned int>(size));
#else
  (void)::write(STDERR_FILENO, message, size);
#endif
  // 不触发崩溃对话框，不运行 atexit/析构，也不试图保存用户文档。
  std::_Exit(EXIT_FAILURE);
}
[[noreturn]] void fatal_current_exception() noexcept {
  try {
    if (auto exception = std::current_exception())
      std::rethrow_exception(exception);
  } catch (const std::bad_alloc&) {
    fatal(fatal_reason::out_of_memory);
  } catch (...) {
    fatal(fatal_reason::unhandled_exception);
  }
  fatal(fatal_reason::unhandled_exception);
}
void install_fatal_handler() noexcept { std::set_terminate(fatal_current_exception); }
}
