// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/ipc/editor_ipc_event.hpp"
#include <gneiss/engine/error.hpp>
#include "runtime/ipc/inbound/event_decode.hpp"

namespace gneiss::host {
gneiss::error decode_runtime_scene_event(const gneiss::ipc::envelope& envelope,
                                                 runtime_ipc_event& output) noexcept {
  auto decoded = protocol::decode(envelope.kind == ipc::message_kind::response ? scene_result : scene_progress, envelope);
  if (!decoded) return decoded.error().error;
  output = runtime_scene_progress_event{envelope.request_id, std::move(*decoded)};
  return error::success;
}
} // namespace gneiss::host
