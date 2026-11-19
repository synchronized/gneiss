// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <functional>
#include <gneiss/engine/core/base/frame_time.hpp>
#include <gneiss/engine/core/base/result.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace gneiss::app {
/** 当前接入业务阶段；平台事件与完成分发仍由 Application 固定驱动。 */
enum class stage { update, synchronize, present };
using system_function = std::function<error(const frame_time&)>;
/** 同步适配，无参数或 frame_time 参数；保留意外异常的传播语义。 */
template <class F> system_function make_system(F function) {
  return [function = std::move(function)](const frame_time& time) mutable -> error {
    auto invoke = [&]() -> decltype(auto) {
      if constexpr (std::is_invocable_v<F&, const frame_time&>)
        return std::invoke(function, time);
      else
        return std::invoke(function);
    };
    using R = decltype(invoke());
    if constexpr (std::is_void_v<R>) {
      invoke();
      return error::success;
    } else if constexpr (std::is_same_v<R, error>) {
      return invoke();
    } else {
      static_assert(std::is_same_v<R, result<void>>);
      auto result = invoke();
      return result ? error::success : result.error();
    }
  };
}
}
