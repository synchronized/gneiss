// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_event.hpp"
#include "engine/core/error.hpp"
#include "event_decode.hpp"

namespace gneiss::host {
gneiss::engine::error decode_runtime_scene_event(const gneiss::engine::ipc_envelope& envelope,
                                                 runtime_ipc_event& output) noexcept {
  return gneiss::host::detail::decode_value<runtime_scene_progress_event>(
      envelope, output, [&](auto& value) { return decode_ipc_scene_progress(envelope, value); });
}
} // namespace gneiss::host
