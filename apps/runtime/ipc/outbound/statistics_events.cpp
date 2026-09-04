// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_outbound.h"

#include "ipc_statistics_protocol.h"

namespace gneiss::runtime_internal {

result make_statistics_event(const ipc_runtime_statistics& statistics,
                             ipc_envelope& output) noexcept {
  return encode_ipc_statistics_v2(statistics, output);
}

} // namespace gneiss::runtime_internal
