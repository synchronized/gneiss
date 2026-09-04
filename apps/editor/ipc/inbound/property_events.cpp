// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_event.h"

#include "event_decode.h"

namespace gneiss::editor {

result decode_runtime_property_event(const ipc_envelope& envelope,
                                     runtime_ipc_event& output) noexcept {
  return ipc_internal::decode_value<runtime_property_result_event>(
      envelope, output,
      [&](auto& value) { return decode_ipc_property_result_v2(envelope, value); });
}

} // namespace gneiss::editor
