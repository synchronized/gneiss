// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/log/log_dispatcher.hpp"
#include <gneiss/engine/application.h>

namespace gneiss::application_internal {
/** callback 须非空；复制回调配置，user_data 须由调用方保持至投递器关闭完成。 */
inline log_internal::event_sink make_application_log_sink(gneiss_application_log_fn callback,
                                                          void* user_data) {
  return [callback, user_data](const log_internal::event_view& event) {
    const auto& message = event.message;
    const gneiss_log_event borrowed{
        .struct_size = sizeof(gneiss_log_event),
        .severity = message.severity,
        .sequence = event.sequence,
        .timestamp_ns = event.timestamp_ns,
        .thread_id = event.thread_id,
        .source = message.source.data(),
        .source_length = message.source.size(),
        .category = message.category.data(),
        .category_length = message.category.size(),
        .message = message.message.data(),
        .message_length = message.message.size(),
        .result = message.result,
        .flags = 0U,
        .reserved = {},
    };
    // 异常由通用投递器隔离；这里不能用 noexcept 提前终止进程。
    callback(message.context, &borrowed, user_data);
  };
}
} // namespace gneiss::application_internal
