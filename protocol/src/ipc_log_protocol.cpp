// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/protocol/ipc_log_protocol.hpp>
#include <gneiss/engine/error.hpp>

#include <array>
#include <new>
#include <utility>

namespace {

constexpr std::size_t max_log_event_size = std::size_t{16U} * 1024U;
constexpr auto event_kind = gneiss::protocol::kind_mask(gneiss::ipc::message_kind::event);
constexpr std::array log_operations{gneiss::protocol::operation_descriptor{
    .operation = static_cast<std::uint16_t>(gneiss::host::ipc_log_operation::event),
    .editor_to_runtime_kinds = 0U,
    .runtime_to_editor_kinds = event_kind}};

} // namespace

namespace gneiss::host {

gneiss::error encode_ipc_log_event(std::string_view event,
                                           gneiss::ipc::envelope& output) {
  if (event.size() > max_log_event_size) {
    return gneiss::error::invalid_argument;
  }
  {
    gneiss::ipc::envelope encoded{
        .domain = gneiss::protocol::domain::log,
        .operation = static_cast<std::uint16_t>(ipc_log_operation::event),
        .kind = gneiss::ipc::message_kind::event,
        .request_id = 0U,
        .payload = {reinterpret_cast<const std::uint8_t*>(event.data()),
                    reinterpret_cast<const std::uint8_t*>(event.data()) + event.size()}};
    output = std::move(encoded);
    return gneiss::error::success;
  }
}

gneiss::error decode_ipc_log_event(const gneiss::ipc::envelope& envelope,
                                           std::string& output) {
  if (envelope.domain != gneiss::protocol::domain::log ||
      envelope.operation != static_cast<std::uint16_t>(ipc_log_operation::event) ||
      envelope.kind != gneiss::ipc::message_kind::event || envelope.request_id != 0U ||
      envelope.payload.size() > max_log_event_size) {
    return gneiss::error::invalid_argument;
  }
  {
    std::string decoded(reinterpret_cast<const char*>(envelope.payload.data()),
                        envelope.payload.size());
    output = std::move(decoded);
    return gneiss::error::success;
  }
}

std::span<const gneiss::protocol::operation_descriptor> ipc_log_operations() noexcept {
  return log_operations;
}

} // namespace gneiss::host
