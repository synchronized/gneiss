// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <array>
#include <gneiss/protocol/ipc_log_protocol.hpp>

namespace gneiss::host {
const protocol::message_descriptor<runtime_log_record> log_record{
    protocol::domain::log,
    static_cast<std::uint16_t>(ipc_log_operation::event),
    ipc::message_kind::event,
    protocol::peer_role::runtime,
    16U * 1024U,
    [](const runtime_log_record& record) { return record.version == 2U; }};

std::span<const protocol::operation_descriptor> ipc_log_operations() noexcept {
  static const std::array operations{log_record.rule()};
  return operations;
}
} // namespace gneiss::host
