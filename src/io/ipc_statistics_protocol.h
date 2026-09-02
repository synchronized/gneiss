// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_IO_IPC_STATISTICS_PROTOCOL_H_
#define GNEISS_SRC_IO_IPC_STATISTICS_PROTOCOL_H_

#include "ipc_protocol.h"

#include <cstdint>

namespace gneiss {

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
                                                   ipc_frame& output) noexcept;
[[nodiscard]] result decode_ipc_runtime_statistics(const ipc_frame& frame,
                                                   ipc_runtime_statistics& output) noexcept;

} // namespace gneiss

#endif
