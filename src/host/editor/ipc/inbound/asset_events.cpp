// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_event.hpp"
#include "engine/core/result.hpp"

#include "event_decode.hpp"

namespace gneiss::host {

gneiss::engine::result decode_runtime_asset_event(const gneiss::engine::ipc_envelope& envelope,
                                                  runtime_ipc_event& output) noexcept {
  return gneiss::host::detail::decode_value<runtime_asset_result_event>(
      envelope, output, [&](auto& value) { return decode_ipc_asset_result_v2(envelope, value); });
}

} // namespace gneiss::host
