// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "runtime_ipc_outbound.hpp"

#include "ipc_statistics_protocol.hpp"

namespace gneiss::host::runtime {

gneiss::engine::core::result
make_statistics_event(const ipc_runtime_statistics& statistics,
                      gneiss::engine::platform::ipc_envelope& output) noexcept {
  return encode_ipc_statistics_v2(statistics, output);
}

} // namespace gneiss::host::runtime
