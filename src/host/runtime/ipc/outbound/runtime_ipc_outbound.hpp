// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_IPC_OUTBOUND_RUNTIME_IPC_OUTBOUND_H_
#define GNEISS_APPS_RUNTIME_IPC_OUTBOUND_RUNTIME_IPC_OUTBOUND_H_

#include "engine/core/result.hpp"
#include "ipc_asset_protocol.hpp"

#include "ipc_control_protocol.hpp"
#include "ipc_inspection_protocol.hpp"
#include "ipc_property_protocol.hpp"
#include "ipc_session_protocol.hpp"
#include "ipc_statistics_protocol.hpp"

#include <gneiss/engine/log.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gneiss::host {

[[nodiscard]] gneiss::engine::result
make_session_hello_event(const std::string& token,
                         const std::vector<gneiss::engine::ipc_domain_capability>& domains,
                         std::uint32_t request_id, gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::result
make_heartbeat_response(const ipc_session_heartbeat& heartbeat, std::uint32_t request_id,
                        gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::result
make_shutdown_event(std::int32_t exit_code, gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::result
make_protocol_error_response(gneiss::engine::result operation, std::string message,
                             std::uint32_t request_id,
                             gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::result
make_ready_event(gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::result
make_state_event(ipc_control_state state, gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::result make_log_event(const gneiss_log_event& event,
                                                    gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::result
make_scene_snapshot_events(const ipc_inspection_batch& batch,
                           std::vector<gneiss::engine::ipc_envelope>& output) noexcept;
[[nodiscard]] gneiss::engine::result
make_property_write_result_event(const ipc_property_write_result& response,
                                 std::uint32_t request_id,
                                 gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::result
make_asset_reload_result(const ipc_asset_reload_result& response, ipc_asset_operation operation,
                         std::uint32_t request_id, gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::result
make_statistics_event(const ipc_runtime_statistics& statistics,
                      gneiss::engine::ipc_envelope& output) noexcept;

} // namespace gneiss::host

#endif
