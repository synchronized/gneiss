// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/fatal.hpp"
#include "engine/core/value_edit.hpp"
#include <array>
#include <cstdlib>
#include <new>

// 独立编译待测实现，避免依赖跨 DLL 的 operator new 替换行为。
namespace {
thread_local int remaining = -1;
}
void* operator new(std::size_t size) {
  if (remaining == 0)
    throw std::bad_alloc{};
  if (remaining > 0)
    --remaining;
  if (auto* p = std::malloc(size ? size : 1))
    return p;
  throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

template <class Operation, class Unchanged>
bool check_allocations(Operation operation, Unchanged unchanged) {
  remaining = 0;
  bool propagated = false;
  try {
    (void)operation();
  } catch (const std::bad_alloc&) {
    propagated = true;
  }
  remaining = -1;
  return propagated && unchanged() && operation() && unchanged();
}

int main(int argc, char**) {
  gneiss::engine::install_fatal_handler();
  using namespace gneiss::engine;
  const auto id = guid::literal("118e3942-1e4c-4e31-8412-95be2edcb8bc");
  const value before{record_value{record_value_data{id, {{1, value{false}}}}}};
  const value after{record_value{record_value_data{id, {{1, value{true}}}}}};
  const value saved{record_value{record_value_data{id, {{1, value{false}}}}}};
  if (argc > 1) {
    // MSVC Debug 的容器代理初始化可能在标准库 noexcept 内分配。
    // 无论由内层 terminate 还是外层捕获到 OOM，均须无弹窗致命退出。
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL == 2
    remaining = 2;
#else
    remaining = 0;
#endif
    try {
      (void)validate_persistent_value(before);
    } catch (...) {
      fatal_current_exception();
    }
    return 9;
  }
  const auto unchanged = [&] { return before == saved; };
  const std::array<value_edit, 1> edits{
      value_edit{{meta::property_id{1}}, set_value_operation{value{true}}}};
  const value_operation op = set_value_operation{before};
  if (!check_allocations([&] { return validate_persistent_value(before).ok(); }, unchanged))
    return 1;
  if (!check_allocations([&] { return validate_value_operation(op).ok(); }, unchanged))
    return 2;
  if (!check_allocations(
          [&] {
            auto result = apply_value_edits(before, edits);
            return result && *result == after;
          },
          unchanged))
    return 3;
  if (!check_allocations(
          [&] {
            auto result = replay_value_edits(before, edits);
            return result && result->root == after && result->conflicts.empty();
          },
          unchanged))
    return 4;
  if (!check_allocations(
          [&] {
            auto result = diff_values(before, after);
            return result && result->size() == 1;
          },
          unchanged))
    return 5;
  return 0;
}
