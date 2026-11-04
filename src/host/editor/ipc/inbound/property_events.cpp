// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_event.hpp"
#include "engine/core/error.hpp"

#include "event_decode.hpp"

namespace gneiss::host {

gneiss::engine::error decode_runtime_property_event(const gneiss::engine::ipc_envelope& envelope,
                                                    runtime_ipc_event& output) noexcept {
  return gneiss::host::detail::decode_value<runtime_property_result_event>(
      envelope, output,
      [&](auto& value) { return decode_ipc_property_result_v2(envelope, value); });
}

} // namespace gneiss::host
