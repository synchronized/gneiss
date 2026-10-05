// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "console_model.hpp"

#include <gneiss/app/runtime_log_protocol.h>

#include <utility>

namespace gneiss::editor {

/** 宿主已完成协议校验；移动日志正文进入控制台，不保留协议对象。 */
[[nodiscard]] inline console_event to_console_event(app::runtime_log_record&& record) noexcept {
  return {.severity = record.severity,
          .sequence = record.sequence,
          .timestamp_ns = record.timestamp_ns,
          .thread_id = record.thread_id,
          .source = std::move(record.source),
          .category = std::move(record.category),
          .message = std::move(record.message),
          .operation = record.operation};
}

} // namespace gneiss::editor
