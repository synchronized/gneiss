// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "runtime_ipc_outbound.hpp"

#include "ipc_inspection_protocol.hpp"

namespace gneiss::host {

gneiss::engine::result
make_scene_snapshot_events(const ipc_inspection_batch& batch,
                           std::vector<gneiss::engine::ipc_envelope>& output) noexcept {
  return encode_ipc_inspection_batch_v2(batch, output);
}

} // namespace gneiss::host
