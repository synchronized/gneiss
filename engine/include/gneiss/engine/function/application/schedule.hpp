// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <functional>
#include <gneiss/engine/core/base/frame_time.hpp>
#include <gneiss/engine/core/base/result.hpp>
#include <gneiss/engine/core/base/type_id.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace gneiss::app {
/** 当前接入业务阶段；平台事件与完成分发仍由 Application 固定驱动。 */
enum class lifecycle { starting, running, stopping, stopped };
enum class stage { update, synchronize, present };
namespace detail {
enum class state_event { enter, update, exit };
struct state_key {
  type_id type;
  std::uint64_t value;
  friend bool operator==(state_key, state_key) = default;
};
template <class T>
  requires std::is_enum_v<T>
state_key key(T state) {
  return {runtime_type_of<T>(), static_cast<std::uint64_t>(state)};
}
}
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
/** 停止步骤可等待既有任务完成；pending 不表示错误。 */
enum class stop_progress { pending, complete };
using stop_function = std::function<result<stop_progress>()>;
template <class F> stop_function make_stop_system(F function) {
  return [function = std::move(function)]() mutable -> result<stop_progress> {
    using R = std::invoke_result_t<F&>;
    if constexpr (std::is_void_v<R>) {
      std::invoke(function);
      return stop_progress::complete;
    } else if constexpr (std::is_same_v<R, stop_progress> ||
                         std::is_same_v<R, result<stop_progress>>) {
      return std::invoke(function);
    } else if constexpr (std::is_same_v<R, error>) {
      auto status = std::invoke(function);
      if (status.failed())
        return status;
      return stop_progress::complete;
    } else {
      static_assert(std::is_same_v<R, result<void>>);
      auto status = std::invoke(function);
      if (!status)
        return status.error();
      return stop_progress::complete;
    }
  };
}

}
