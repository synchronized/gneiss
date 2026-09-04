// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_outbound.h"

#include "ipc_inspection_protocol.h"

namespace gneiss::runtime_internal {

result make_scene_snapshot_events(const ipc_inspection_batch& batch,
                                  std::vector<ipc_envelope>& output) noexcept {
  return encode_ipc_inspection_batch_v2(batch, output);
}

} // namespace gneiss::runtime_internal
