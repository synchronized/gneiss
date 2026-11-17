// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_IPC_OUTBOUND_EDITOR_IPC_OUTBOUND_H_
#define GNEISS_APPS_EDITOR_IPC_OUTBOUND_EDITOR_IPC_OUTBOUND_H_

#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/ipc_asset_protocol.hpp>
#include <gneiss/engine/platform/ipc/envelope.hpp>
#include <gneiss/protocol/ipc_property_protocol.hpp>

namespace gneiss::host {

[[nodiscard]] gneiss::error
make_pause_command(std::uint32_t request_id, gneiss::ipc::envelope& output) noexcept;
[[nodiscard]] gneiss::error
make_resume_command(std::uint32_t request_id, gneiss::ipc::envelope& output) noexcept;
[[nodiscard]] gneiss::error
make_stop_command(std::uint32_t request_id, gneiss::ipc::envelope& output) noexcept;
[[nodiscard]] gneiss::error make_heartbeat_command(std::uint64_t nonce,
                                                           std::uint32_t request_id,
                                                           gneiss::ipc::envelope& output);
[[nodiscard]] gneiss::error
make_inspection_resync_command(std::uint32_t request_id,
                               gneiss::ipc::envelope& output) noexcept;
[[nodiscard]] gneiss::error
make_property_write_command(const ipc_property_write& command,
                            gneiss::ipc::envelope& output) noexcept;

} // namespace gneiss::host

#endif
