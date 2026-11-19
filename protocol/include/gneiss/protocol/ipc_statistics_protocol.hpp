// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_DOMAINS_STATISTICS_IPC_STATISTICS_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_DOMAINS_STATISTICS_IPC_STATISTICS_PROTOCOL_H_

#include <gneiss/protocol/message.hpp>

#include <gneiss/engine/core/base/error.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace gneiss::host {

inline constexpr std::uint16_t ipc_statistics_domain_version = 1U;
enum class ipc_statistics_operation : std::uint16_t { snapshot = 1U };

struct ipc_runtime_statistics final {
  std::uint64_t session_id = 0U;
  std::uint64_t sequence = 0U;
  std::uint64_t frame_index = 0U;
  std::uint64_t frame_delta_ns = 0U;
  std::uint64_t scene_node_count = 0U;
  std::uint64_t entity_count = 0U;
  std::uint64_t ipc_pending_writes = 0U;
  std::uint64_t ipc_dropped_events = 0U;
};

extern const protocol::message_descriptor<ipc_runtime_statistics> statistics_snapshot;

[[nodiscard]] std::span<const gneiss::protocol::operation_descriptor>
ipc_statistics_operations() noexcept;

} // namespace gneiss::host

#endif
