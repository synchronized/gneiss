// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_DOMAINS_STATISTICS_IPC_STATISTICS_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_DOMAINS_STATISTICS_IPC_STATISTICS_PROTOCOL_H_

#include "ipc_dispatcher.h"

#include <gneiss/core/result.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace gneiss {

inline constexpr std::uint16_t ipc_statistics_domain_version = 1U;
enum class ipc_statistics_operation : std::uint16_t { snapshot = 1U };

struct ipc_runtime_statistics final {
  std::uint64_t session_id = 0U;
  std::uint64_t sequence = 0U;
  std::uint64_t frame_index = 0U;
  std::uint64_t frame_delta_ns = 0U;
  std::uint64_t fixed_update_count = 0U;
  std::uint64_t scene_node_count = 0U;
  std::uint64_t entity_count = 0U;
  std::uint64_t ipc_pending_writes = 0U;
  std::uint64_t ipc_dropped_events = 0U;
};

[[nodiscard]] result encode_ipc_runtime_statistics(const ipc_runtime_statistics& statistics,
                                                   std::vector<std::uint8_t>& output) noexcept;
[[nodiscard]] result decode_ipc_runtime_statistics(std::span<const std::uint8_t> payload,
                                                   ipc_runtime_statistics& output) noexcept;

[[nodiscard]] result encode_ipc_statistics_v2(const ipc_runtime_statistics& statistics,
                                              ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_statistics_v2(const ipc_envelope& envelope,
                                              ipc_runtime_statistics& output) noexcept;
[[nodiscard]] std::span<const ipc_operation_descriptor> ipc_statistics_operations() noexcept;

} // namespace gneiss

#endif
