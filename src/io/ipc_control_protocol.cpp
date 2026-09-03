// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_control_protocol.h"

#include <array>
#include <new>
#include <utility>

namespace {

constexpr std::array control_operations{
    gneiss::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::ipc_control_operation::ready),
        .editor_to_runtime_kinds = 0U,
        .runtime_to_editor_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::event)},
    gneiss::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::ipc_control_operation::state_changed),
        .editor_to_runtime_kinds = 0U,
        .runtime_to_editor_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::event)},
    gneiss::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::ipc_control_operation::pause),
        .editor_to_runtime_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::request),
        .runtime_to_editor_kinds = 0U},
    gneiss::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::ipc_control_operation::resume),
        .editor_to_runtime_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::request),
        .runtime_to_editor_kinds = 0U},
    gneiss::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::ipc_control_operation::stop),
        .editor_to_runtime_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::request),
        .runtime_to_editor_kinds = 0U}};

[[nodiscard]] bool valid_state(gneiss::ipc_control_state state) noexcept {
  return state >= gneiss::ipc_control_state::loading &&
         state <= gneiss::ipc_control_state::stopping;
}

[[nodiscard]] bool valid_request(gneiss::ipc_control_operation operation) noexcept {
  return operation == gneiss::ipc_control_operation::pause ||
         operation == gneiss::ipc_control_operation::resume ||
         operation == gneiss::ipc_control_operation::stop;
}

} // namespace

namespace gneiss {

result encode_ipc_control_ready(ipc_envelope& output) noexcept {
  output = {.domain = ipc_domain::control,
            .operation = static_cast<std::uint16_t>(ipc_control_operation::ready),
            .kind = ipc_message_kind::event,
            .request_id = 0U,
            .payload = {}};
  return result::success;
}

result encode_ipc_control_state(ipc_control_state state, ipc_envelope& output) noexcept {
  if (!valid_state(state)) {
    return result::invalid_argument;
  }
  try {
    ipc_envelope encoded{.domain = ipc_domain::control,
                         .operation =
                             static_cast<std::uint16_t>(ipc_control_operation::state_changed),
                         .kind = ipc_message_kind::event,
                         .request_id = 0U,
                         .payload = {static_cast<std::uint8_t>(state)}};
    output = std::move(encoded);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_control_state(const ipc_envelope& envelope, ipc_control_state& output) noexcept {
  if (envelope.domain != ipc_domain::control ||
      envelope.operation != static_cast<std::uint16_t>(ipc_control_operation::state_changed) ||
      envelope.kind != ipc_message_kind::event || envelope.request_id != 0U ||
      envelope.payload.size() != 1U) {
    return result::invalid_argument;
  }
  const auto state = static_cast<ipc_control_state>(envelope.payload.front());
  if (!valid_state(state)) {
    return result::invalid_argument;
  }
  output = state;
  return result::success;
}

result encode_ipc_control_request(ipc_control_operation operation, std::uint32_t request_id,
                                  ipc_envelope& output) noexcept {
  if (!valid_request(operation) || request_id == 0U) {
    return result::invalid_argument;
  }
  output = {.domain = ipc_domain::control,
            .operation = static_cast<std::uint16_t>(operation),
            .kind = ipc_message_kind::request,
            .request_id = request_id,
            .payload = {}};
  return result::success;
}

result decode_ipc_control_request(const ipc_envelope& envelope,
                                  ipc_control_operation& output) noexcept {
  const auto operation = static_cast<ipc_control_operation>(envelope.operation);
  if (envelope.domain != ipc_domain::control || !valid_request(operation) ||
      envelope.kind != ipc_message_kind::request || envelope.request_id == 0U ||
      !envelope.payload.empty()) {
    return result::invalid_argument;
  }
  output = operation;
  return result::success;
}

std::span<const ipc_operation_descriptor> ipc_control_operations() noexcept {
  return control_operations;
}

} // namespace gneiss
