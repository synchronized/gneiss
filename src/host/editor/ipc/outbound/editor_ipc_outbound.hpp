// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_IPC_OUTBOUND_EDITOR_IPC_OUTBOUND_H_
#define GNEISS_APPS_EDITOR_IPC_OUTBOUND_EDITOR_IPC_OUTBOUND_H_

#include "engine/core/result.hpp"
#include "ipc_asset_protocol.hpp"
#include "ipc_envelope.hpp"
#include "ipc_property_protocol.hpp"

namespace gneiss::editor {

[[nodiscard]] gneiss::engine::core::result
make_pause_command(std::uint32_t request_id,
                   gneiss::engine::platform::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::core::result
make_resume_command(std::uint32_t request_id,
                    gneiss::engine::platform::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::core::result
make_stop_command(std::uint32_t request_id,
                  gneiss::engine::platform::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::core::result
make_heartbeat_command(std::uint64_t nonce, std::uint32_t request_id,
                       gneiss::engine::platform::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::core::result
make_inspection_resync_command(std::uint32_t request_id,
                               gneiss::engine::platform::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::core::result
make_property_write_command(const ipc_property_write& command,
                            gneiss::engine::platform::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::core::result
make_asset_reload_command(const ipc_asset_reload_request& command, ipc_asset_operation operation,
                          std::uint32_t request_id,
                          gneiss::engine::platform::ipc_envelope& output) noexcept;

} // namespace gneiss::editor

#endif
