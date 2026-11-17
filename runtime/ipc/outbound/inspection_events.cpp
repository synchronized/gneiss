// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include "ipc/outbound/runtime_ipc_outbound.hpp"

#include <gneiss/protocol/ipc_inspection_protocol.hpp>

namespace gneiss::host {

gneiss::error
make_scene_snapshot_events(const ipc_inspection_batch& batch,
                           std::vector<gneiss::ipc::envelope>& output) {
  return encode_ipc_inspection_batch_v2(batch, output);
}

} // namespace gneiss::host
