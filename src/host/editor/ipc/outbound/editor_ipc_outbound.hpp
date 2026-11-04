// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_IPC_OUTBOUND_EDITOR_IPC_OUTBOUND_H_
#define GNEISS_APPS_EDITOR_IPC_OUTBOUND_EDITOR_IPC_OUTBOUND_H_

#include "engine/core/error.hpp"
#include "ipc_asset_protocol.hpp"
#include "ipc_envelope.hpp"
#include "ipc_property_protocol.hpp"

namespace gneiss::host {

[[nodiscard]] gneiss::engine::error
make_pause_command(std::uint32_t request_id, gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::error
make_resume_command(std::uint32_t request_id, gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::error
make_stop_command(std::uint32_t request_id, gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::error
make_heartbeat_command(std::uint64_t nonce, std::uint32_t request_id,
                       gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::error
make_inspection_resync_command(std::uint32_t request_id,
                               gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::error
make_property_write_command(const ipc_property_write& command,
                            gneiss::engine::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::error
make_asset_reload_command(const ipc_asset_reload_request& command, ipc_asset_operation operation,
                          std::uint32_t request_id, gneiss::engine::ipc_envelope& output) noexcept;

} // namespace gneiss::host

#endif
