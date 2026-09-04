// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_log_protocol.h"

#include <array>
#include <new>
#include <utility>

namespace {

constexpr std::size_t max_log_event_size = std::size_t{16U} * 1024U;
constexpr auto event_kind = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::event);
constexpr std::array log_operations{gneiss::ipc_operation_descriptor{
    .operation = static_cast<std::uint16_t>(gneiss::ipc_log_operation::event),
    .editor_to_runtime_kinds = 0U,
    .runtime_to_editor_kinds = event_kind}};

} // namespace

namespace gneiss {

result encode_ipc_log_event(std::string_view event, ipc_envelope& output) noexcept {
  if (event.size() > max_log_event_size) {
    return result::invalid_argument;
  }
  try {
    ipc_envelope encoded{
        .domain = ipc_domain::log,
        .operation = static_cast<std::uint16_t>(ipc_log_operation::event),
        .kind = ipc_message_kind::event,
        .request_id = 0U,
        .payload = {reinterpret_cast<const std::uint8_t*>(event.data()),
                    reinterpret_cast<const std::uint8_t*>(event.data()) + event.size()}};
    output = std::move(encoded);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_log_event(const ipc_envelope& envelope, std::string& output) noexcept {
  if (envelope.domain != ipc_domain::log ||
      envelope.operation != static_cast<std::uint16_t>(ipc_log_operation::event) ||
      envelope.kind != ipc_message_kind::event || envelope.request_id != 0U ||
      envelope.payload.size() > max_log_event_size) {
    return result::invalid_argument;
  }
  try {
    std::string decoded(reinterpret_cast<const char*>(envelope.payload.data()),
                        envelope.payload.size());
    output = std::move(decoded);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

std::span<const ipc_operation_descriptor> ipc_log_operations() noexcept { return log_operations; }

} // namespace gneiss
