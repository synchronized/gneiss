// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_outbound.hpp"
#include "engine/core/error.hpp"

#include "ipc_inspection_protocol.hpp"

namespace gneiss::host {

gneiss::engine::error
make_inspection_resync_command(std::uint32_t request_id,
                               gneiss::engine::ipc_envelope& output) noexcept {
  return encode_ipc_inspection_resync(request_id, output);
}

} // namespace gneiss::host
