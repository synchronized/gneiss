// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_IPC_INBOUND_EVENT_DECODE_H_
#define GNEISS_APPS_EDITOR_IPC_INBOUND_EVENT_DECODE_H_

#include "editor_ipc_event.hpp"
#include "engine/core/error.hpp"

#include <utility>

namespace gneiss::host::detail {

template <typename Event, typename Decode>
gneiss::error decode_value(const gneiss::ipc_envelope& envelope,
                                   runtime_ipc_event& output, Decode&& decode) noexcept {
  Event event;
  event.request_id = envelope.request_id;
  const auto operation = decode(event.value);
  if (operation == gneiss::error::success) {
    output = std::move(event);
  }
  return operation;
}

} // namespace gneiss::host::detail

#endif
