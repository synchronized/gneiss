// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/ipc_control_protocol.hpp>

#include <array>
#include <gneiss/protocol/payload.hpp>
#include <utility>

namespace gneiss::host {
const protocol::message_descriptor<ipc_control_state_message> control_state_changed{
    protocol::domain::control,
    static_cast<std::uint16_t>(ipc_control_operation::state_changed),
    ipc::message_kind::event,
    protocol::peer_role::runtime,
    ipc_control_max_payload_size,
    [](const ipc_control_state_message& value) {
      return value.state >= ipc_control_state::loading &&
             value.state <= ipc_control_state::stopping;
    }};
}

namespace {

const std::array control_operations{
    gneiss::protocol::operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::host::ipc_control_operation::ready),
        .editor_to_runtime_kinds = 0U,
        .runtime_to_editor_kinds = gneiss::protocol::kind_mask(gneiss::ipc::message_kind::event)},
    gneiss::host::control_state_changed.rule(),
    gneiss::protocol::operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::host::ipc_control_operation::pause),
        .editor_to_runtime_kinds = gneiss::protocol::kind_mask(gneiss::ipc::message_kind::request),
        .runtime_to_editor_kinds = 0U},
    gneiss::protocol::operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::host::ipc_control_operation::resume),
        .editor_to_runtime_kinds = gneiss::protocol::kind_mask(gneiss::ipc::message_kind::request),
        .runtime_to_editor_kinds = 0U},
    gneiss::protocol::operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::host::ipc_control_operation::stop),
        .editor_to_runtime_kinds = gneiss::protocol::kind_mask(gneiss::ipc::message_kind::request),
        .runtime_to_editor_kinds = 0U}};

[[nodiscard]] bool valid_request(gneiss::host::ipc_control_operation operation) noexcept {
  return operation == gneiss::host::ipc_control_operation::pause ||
         operation == gneiss::host::ipc_control_operation::resume ||
         operation == gneiss::host::ipc_control_operation::stop;
}

} // namespace

namespace gneiss::host {

gneiss::error encode_ipc_control_ready(gneiss::ipc::envelope& output) noexcept {
  output = {.domain = gneiss::protocol::domain::control,
            .operation = static_cast<std::uint16_t>(ipc_control_operation::ready),
            .kind = gneiss::ipc::message_kind::event,
            .request_id = 0U,
            .payload = {}};
  return gneiss::error::success;
}

gneiss::error encode_ipc_control_request(ipc_control_operation operation, std::uint32_t request_id,
                                         gneiss::ipc::envelope& output) noexcept {
  if (!valid_request(operation) || request_id == 0U) {
    return gneiss::error::invalid_argument;
  }
  output = {.domain = gneiss::protocol::domain::control,
            .operation = static_cast<std::uint16_t>(operation),
            .kind = gneiss::ipc::message_kind::request,
            .request_id = request_id,
            .payload = {}};
  return gneiss::error::success;
}

gneiss::error decode_ipc_control_request(const gneiss::ipc::envelope& envelope,
                                         ipc_control_operation& output) noexcept {
  const auto operation = static_cast<ipc_control_operation>(envelope.operation);
  if (envelope.domain != gneiss::protocol::domain::control || !valid_request(operation) ||
      envelope.kind != gneiss::ipc::message_kind::request || envelope.request_id == 0U ||
      !envelope.payload.empty()) {
    return gneiss::error::invalid_argument;
  }
  output = operation;
  return gneiss::error::success;
}

std::span<const gneiss::protocol::operation_descriptor> ipc_control_operations() noexcept {
  return control_operations;
}

} // namespace gneiss::host
