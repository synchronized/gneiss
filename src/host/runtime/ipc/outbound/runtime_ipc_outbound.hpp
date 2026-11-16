// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_IPC_OUTBOUND_RUNTIME_IPC_OUTBOUND_H_
#define GNEISS_APPS_RUNTIME_IPC_OUTBOUND_RUNTIME_IPC_OUTBOUND_H_

#include <gneiss/engine/error.hpp>
#include "ipc_asset_protocol.hpp"

#include "ipc_control_protocol.hpp"
#include "ipc_inspection_protocol.hpp"
#include "ipc_property_protocol.hpp"
#include "ipc_session_protocol.hpp"
#include "ipc_statistics_protocol.hpp"

#include <gneiss/engine/core/logging/record.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace gneiss::host {

[[nodiscard]] gneiss::error
make_session_hello_event(const std::string& token,
                         const std::vector<gneiss::ipc_domain_capability>& domains,
                         std::uint32_t request_id, gneiss::ipc_envelope& output);
[[nodiscard]] gneiss::error make_heartbeat_response(const ipc_session_heartbeat& heartbeat,
                                                            std::uint32_t request_id,
                                                            gneiss::ipc_envelope& output);
[[nodiscard]] gneiss::error make_shutdown_event(std::int32_t exit_code,
                                                        gneiss::ipc_envelope& output);
[[nodiscard]] gneiss::error
make_protocol_error_response(gneiss::error operation, std::string message,
                             std::uint32_t request_id, gneiss::ipc_envelope& output);
[[nodiscard]] gneiss::error make_ready_event(gneiss::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::error make_state_event(ipc_control_state state,
                                                     gneiss::ipc_envelope& output);
[[nodiscard]] gneiss::error
make_log_event(const gneiss::logging::record_view& event,
               gneiss::ipc_envelope& output);
[[nodiscard]] gneiss::error
make_scene_snapshot_events(const ipc_inspection_batch& batch,
                           std::vector<gneiss::ipc_envelope>& output);
[[nodiscard]] gneiss::error
make_property_write_result_event(const ipc_property_write_result& response,
                                 std::uint32_t request_id,
                                 gneiss::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::error
make_asset_reload_result(const ipc_asset_reload_result& response, ipc_asset_operation operation,
                         std::uint32_t request_id, gneiss::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::error
make_statistics_event(const ipc_runtime_statistics& statistics,
                      gneiss::ipc_envelope& output) noexcept;

} // namespace gneiss::host

#endif
