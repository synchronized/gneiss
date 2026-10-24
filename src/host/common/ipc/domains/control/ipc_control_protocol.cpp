// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_control_protocol.hpp"
#include "engine/core/result.hpp"

#include <array>
#include <new>
#include <utility>

namespace {

constexpr std::array control_operations{
    gneiss::engine::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::host::ipc_control_operation::ready),
        .editor_to_runtime_kinds = 0U,
        .runtime_to_editor_kinds =
            gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::event)},
    gneiss::engine::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::host::ipc_control_operation::state_changed),
        .editor_to_runtime_kinds = 0U,
        .runtime_to_editor_kinds =
            gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::event)},
    gneiss::engine::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::host::ipc_control_operation::pause),
        .editor_to_runtime_kinds =
            gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::request),
        .runtime_to_editor_kinds = 0U},
    gneiss::engine::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::host::ipc_control_operation::resume),
        .editor_to_runtime_kinds =
            gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::request),
        .runtime_to_editor_kinds = 0U},
    gneiss::engine::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::host::ipc_control_operation::stop),
        .editor_to_runtime_kinds =
            gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::request),
        .runtime_to_editor_kinds = 0U}};

[[nodiscard]] bool valid_state(gneiss::host::ipc_control_state state) noexcept {
  return state >= gneiss::host::ipc_control_state::loading &&
         state <= gneiss::host::ipc_control_state::stopping;
}

[[nodiscard]] bool valid_request(gneiss::host::ipc_control_operation operation) noexcept {
  return operation == gneiss::host::ipc_control_operation::pause ||
         operation == gneiss::host::ipc_control_operation::resume ||
         operation == gneiss::host::ipc_control_operation::stop;
}

} // namespace

namespace gneiss::host {

gneiss::engine::result encode_ipc_control_ready(gneiss::engine::ipc_envelope& output) noexcept {
  output = {.domain = gneiss::engine::ipc_domain::control,
            .operation = static_cast<std::uint16_t>(ipc_control_operation::ready),
            .kind = gneiss::engine::ipc_message_kind::event,
            .request_id = 0U,
            .payload = {}};
  return gneiss::engine::result::success;
}

gneiss::engine::result encode_ipc_control_state(ipc_control_state state,
                                                gneiss::engine::ipc_envelope& output) noexcept {
  if (!valid_state(state)) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    gneiss::engine::ipc_envelope encoded{
        .domain = gneiss::engine::ipc_domain::control,
        .operation = static_cast<std::uint16_t>(ipc_control_operation::state_changed),
        .kind = gneiss::engine::ipc_message_kind::event,
        .request_id = 0U,
        .payload = {static_cast<std::uint8_t>(state)}};
    output = std::move(encoded);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result decode_ipc_control_state(const gneiss::engine::ipc_envelope& envelope,
                                                ipc_control_state& output) noexcept {
  if (envelope.domain != gneiss::engine::ipc_domain::control ||
      envelope.operation != static_cast<std::uint16_t>(ipc_control_operation::state_changed) ||
      envelope.kind != gneiss::engine::ipc_message_kind::event || envelope.request_id != 0U ||
      envelope.payload.size() != 1U) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto state = static_cast<ipc_control_state>(envelope.payload.front());
  if (!valid_state(state)) {
    return gneiss::engine::result::invalid_argument;
  }
  output = state;
  return gneiss::engine::result::success;
}

gneiss::engine::result encode_ipc_control_request(ipc_control_operation operation,
                                                  std::uint32_t request_id,
                                                  gneiss::engine::ipc_envelope& output) noexcept {
  if (!valid_request(operation) || request_id == 0U) {
    return gneiss::engine::result::invalid_argument;
  }
  output = {.domain = gneiss::engine::ipc_domain::control,
            .operation = static_cast<std::uint16_t>(operation),
            .kind = gneiss::engine::ipc_message_kind::request,
            .request_id = request_id,
            .payload = {}};
  return gneiss::engine::result::success;
}

gneiss::engine::result decode_ipc_control_request(const gneiss::engine::ipc_envelope& envelope,
                                                  ipc_control_operation& output) noexcept {
  const auto operation = static_cast<ipc_control_operation>(envelope.operation);
  if (envelope.domain != gneiss::engine::ipc_domain::control || !valid_request(operation) ||
      envelope.kind != gneiss::engine::ipc_message_kind::request || envelope.request_id == 0U ||
      !envelope.payload.empty()) {
    return gneiss::engine::result::invalid_argument;
  }
  output = operation;
  return gneiss::engine::result::success;
}

std::span<const gneiss::engine::ipc_operation_descriptor> ipc_control_operations() noexcept {
  return control_operations;
}

} // namespace gneiss::host
