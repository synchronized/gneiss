// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <array>
#include <gneiss/protocol/ipc_statistics_protocol.hpp>
namespace gneiss::host {
const protocol::message_descriptor<ipc_runtime_statistics> statistics_snapshot{
    protocol::domain::statistics,
    static_cast<std::uint16_t>(ipc_statistics_operation::snapshot),
    ipc::message_kind::event,
    protocol::peer_role::runtime,
    64U * 1024U,
    [](const ipc_runtime_statistics& value) {
      return value.session_id != 0 && value.sequence != 0;
    }};
std::span<const protocol::operation_descriptor> ipc_statistics_operations() noexcept {
  static const std::array operations{statistics_snapshot.rule()};
  return operations;
}
} // namespace gneiss::host
