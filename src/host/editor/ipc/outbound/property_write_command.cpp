// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_outbound.hpp"
#include "engine/core/result.hpp"

#include "ipc_property_protocol.hpp"

namespace gneiss::host {

gneiss::engine::result make_property_write_command(const ipc_property_write& command,
                                                   gneiss::engine::ipc_envelope& output) noexcept {
  return encode_ipc_property_write_v2(command, static_cast<std::uint32_t>(command.command_id),
                                      output);
}

} // namespace gneiss::host
