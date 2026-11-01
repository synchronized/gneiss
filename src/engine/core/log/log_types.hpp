// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/error.hpp"
#include <cstdint>
#include <string_view>

namespace gneiss::engine {
/** 原生日志级别；ABI 数值映射由接口层校验。 */
enum class level : std::uint32_t { trace = 1, debug, info, warning, error, fatal };

/** 提交期间借用文本；context 由调用方解释，零表示无关联上下文。 */
struct message_view final {
  std::uint64_t context{};
  level severity{level::info};
  std::string_view source{};
  std::string_view category{};
  std::string_view message{};
  engine::error result{engine::error::success};
};

/** 所有文本只在回调期间有效，接收方不得保留视图。 */
struct event_view final {
  message_view message;
  std::uint64_t sequence{};
  std::uint64_t timestamp_ns{};
  std::uint64_t thread_id{};
};
}
