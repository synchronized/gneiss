// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_IPC_HANDLERS_EVENT_DECODE_H_
#define GNEISS_APPS_EDITOR_IPC_HANDLERS_EVENT_DECODE_H_

#include "editor_ipc_event.h"

#include <utility>

namespace gneiss::editor::ipc_internal {

template <typename Event, typename Decode>
result decode_value(const ipc_envelope& envelope, runtime_ipc_event& output,
                    Decode&& decode) noexcept {
  Event event;
  event.request_id = envelope.request_id;
  const auto operation = decode(event.value);
  if (operation == result::success) {
    output = std::move(event);
  }
  return operation;
}

} // namespace gneiss::editor::ipc_internal

#endif
