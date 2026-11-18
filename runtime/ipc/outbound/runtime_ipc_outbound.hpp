// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_IPC_OUTBOUND_RUNTIME_IPC_OUTBOUND_H_
#define GNEISS_APPS_RUNTIME_IPC_OUTBOUND_RUNTIME_IPC_OUTBOUND_H_

#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/ipc_asset_protocol.hpp>

#include <gneiss/protocol/ipc_control_protocol.hpp>
#include <gneiss/protocol/ipc_inspection_protocol.hpp>
#include <gneiss/protocol/ipc_property_protocol.hpp>
#include <gneiss/protocol/ipc_session_protocol.hpp>
#include <gneiss/protocol/ipc_statistics_protocol.hpp>

#include <gneiss/engine/core/logging/record.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace gneiss::host {

[[nodiscard]] gneiss::error make_ready_event(gneiss::ipc::envelope& output) noexcept;
[[nodiscard]] gneiss::error make_state_event(ipc_control_state state,
                                             gneiss::ipc::envelope& output);
[[nodiscard]] gneiss::error make_log_event(const gneiss::logging::record_view& event,
                                           gneiss::ipc::envelope& output);
[[nodiscard]] gneiss::error make_statistics_event(const ipc_runtime_statistics& statistics,
                                                  gneiss::ipc::envelope& output) noexcept;

} // namespace gneiss::host

#endif
