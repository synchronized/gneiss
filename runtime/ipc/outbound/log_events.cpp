// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc/outbound/runtime_ipc_outbound.hpp"
#include <gneiss/engine/error.hpp>

#include <gneiss/protocol/ipc_log_protocol.hpp>

#include <gneiss/protocol/runtime_log_protocol.hpp>

#include <new>
#include <string>

namespace gneiss::host {

gneiss::error make_log_event(const gneiss::logging::record_view& event,
                             gneiss::ipc::envelope& output) {
  const runtime_log_record record{.version = 2U,
                                  .severity = static_cast<std::uint32_t>(event.message.severity),
                                  .sequence = event.sequence,
                                  .timestamp_ns = event.timestamp_ns,
                                  .thread_id = event.thread_id,
                                  .context = event.message.context,
                                  .source = std::string(event.message.source),
                                  .category = std::string(event.message.category),
                                  .message = std::string(event.message.message),
                                  .operation = event.message.result};
  auto encoded = protocol::encode(log_record, record);
  if (!encoded)
    return encoded.error().error;
  output = std::move(*encoded);
  return error::success;
}
} // namespace gneiss::host
