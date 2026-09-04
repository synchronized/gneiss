// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_IPC_OUTBOUND_RUNTIME_IPC_OUTBOUND_H_
#define GNEISS_APPS_RUNTIME_IPC_OUTBOUND_RUNTIME_IPC_OUTBOUND_H_

#include "ipc_control_protocol.h"
#include "ipc_inspection_protocol.h"
#include "ipc_property_protocol.h"
#include "ipc_session_protocol.h"
#include "ipc_statistics_protocol.h"

#include <gneiss/log.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gneiss::runtime_internal {

[[nodiscard]] result make_session_hello_event(const std::string& token,
                                              const std::vector<ipc_domain_capability>& domains,
                                              std::uint32_t request_id,
                                              ipc_envelope& output) noexcept;
[[nodiscard]] result make_heartbeat_response(const ipc_session_heartbeat& heartbeat,
                                             std::uint32_t request_id,
                                             ipc_envelope& output) noexcept;
[[nodiscard]] result make_shutdown_event(std::int32_t exit_code, ipc_envelope& output) noexcept;
[[nodiscard]] result make_protocol_error_response(result operation, std::string message,
                                                  std::uint32_t request_id,
                                                  ipc_envelope& output) noexcept;
[[nodiscard]] result make_ready_event(ipc_envelope& output) noexcept;
[[nodiscard]] result make_state_event(ipc_control_state state, ipc_envelope& output) noexcept;
[[nodiscard]] result make_log_event(const gneiss_log_event& event, ipc_envelope& output) noexcept;
[[nodiscard]] result make_scene_snapshot_events(const ipc_inspection_batch& batch,
                                                std::vector<ipc_envelope>& output) noexcept;
[[nodiscard]] result make_property_write_result_event(const ipc_property_write_result& response,
                                                      std::uint32_t request_id,
                                                      ipc_envelope& output) noexcept;
[[nodiscard]] result make_statistics_event(const ipc_runtime_statistics& statistics,
                                           ipc_envelope& output) noexcept;

} // namespace gneiss::runtime_internal

#endif
