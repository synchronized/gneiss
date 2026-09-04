// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_commands.h"

#include "ipc_data_protocol.h"

namespace gneiss::editor {

result make_property_write_command(const ipc_property_write& command,
                                   ipc_envelope& output) noexcept {
  return encode_ipc_property_write_v2(command, static_cast<std::uint32_t>(command.command_id),
                                      output);
}

} // namespace gneiss::editor
