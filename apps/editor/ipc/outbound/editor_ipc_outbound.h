// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_IPC_OUTBOUND_EDITOR_IPC_OUTBOUND_H_
#define GNEISS_APPS_EDITOR_IPC_OUTBOUND_EDITOR_IPC_OUTBOUND_H_

#include "ipc_envelope.h"
#include "ipc_property_protocol.h"

namespace gneiss::editor {

[[nodiscard]] result make_pause_command(std::uint32_t request_id, ipc_envelope& output) noexcept;
[[nodiscard]] result make_resume_command(std::uint32_t request_id, ipc_envelope& output) noexcept;
[[nodiscard]] result make_stop_command(std::uint32_t request_id, ipc_envelope& output) noexcept;
[[nodiscard]] result make_heartbeat_command(std::uint64_t nonce, std::uint32_t request_id,
                                            ipc_envelope& output) noexcept;
[[nodiscard]] result make_inspection_resync_command(std::uint32_t request_id,
                                                    ipc_envelope& output) noexcept;
[[nodiscard]] result make_property_write_command(const ipc_property_write& command,
                                                 ipc_envelope& output) noexcept;

} // namespace gneiss::editor

#endif
