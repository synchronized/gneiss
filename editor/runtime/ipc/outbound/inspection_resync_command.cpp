// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/ipc/outbound/editor_ipc_outbound.hpp"
#include <gneiss/engine/core/base/error.hpp>

#include <gneiss/protocol/ipc_inspection_protocol.hpp>

namespace gneiss::host {

gneiss::error
make_inspection_resync_command(std::uint32_t request_id,
                               gneiss::ipc::envelope& output) noexcept {
  return encode_ipc_inspection_resync(request_id, output);
}

} // namespace gneiss::host
