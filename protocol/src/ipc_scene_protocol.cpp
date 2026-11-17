// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/ipc_scene_protocol.hpp>
#include <gneiss/protocol/payload.hpp>

#include <array>
#include <utility>

namespace gneiss::host {
namespace {
bool valid_source(const ipc_scene_request& value) {
  return value.session != 0U && value.revision != 0U && value.uri.starts_with("asset://") &&
         value.uri.size() > 8U && value.uri.size() <= 2048U &&
         value.uri.find("..") == std::string::npos && value.uri.find('\0') == std::string::npos;
}
bool valid_progress(const ipc_scene_progress& value) {
  return valid_source(value.source) && value.phase <= ipc_scene_phase::cancelled &&
         value.total <= 65536U && value.completed <= value.total && value.message.size() <= 4096U &&
         (!value.can_cancel || !scene_phase_terminal(value.phase)) &&
         (!value.budget || !value.budget->cleanup_pending || !value.budget->cleanup_complete);
}
constexpr auto request_kind = gneiss::protocol::kind_mask(gneiss::ipc::message_kind::request);
constexpr auto event_kind = gneiss::protocol::kind_mask(gneiss::ipc::message_kind::event);
constexpr std::array operations{
    gneiss::protocol::operation_descriptor{
        .operation = 1U,
        .editor_to_runtime_kinds = request_kind,
        .runtime_to_editor_kinds = gneiss::protocol::kind_mask(gneiss::ipc::message_kind::response),
    },
    gneiss::protocol::operation_descriptor{
        .operation = 2U,
        .editor_to_runtime_kinds = event_kind,
        .runtime_to_editor_kinds = 0U,
    },
    gneiss::protocol::operation_descriptor{
        .operation = 3U,
        .editor_to_runtime_kinds = 0U,
        .runtime_to_editor_kinds = event_kind,
    },
};
}
bool apply_scene_cleanup_update(ipc_scene_progress& current,
                                const ipc_scene_progress& incoming) noexcept {
  if (current.phase != ipc_scene_phase::cancelled || incoming.phase != current.phase ||
      current.source.session != incoming.source.session ||
      current.source.revision != incoming.source.revision ||
      current.source.uri != incoming.source.uri || !current.budget ||
      !current.budget->cleanup_pending || !incoming.budget || !incoming.budget->cleanup_complete ||
      incoming.budget->cleanup_pending) {
    return false;
  }
  current.budget = incoming.budget;
  return true;
}
bool scene_phase_terminal(ipc_scene_phase phase) noexcept {
  return phase == ipc_scene_phase::applied || phase == ipc_scene_phase::failed ||
         phase == ipc_scene_phase::cancelled;
}
std::span<const gneiss::protocol::operation_descriptor> ipc_scene_operations() noexcept {
  return operations;
}
gneiss::error encode_ipc_scene_request(const ipc_scene_request& value,
                                       ipc_scene_operation operation, std::uint32_t request_id,
                                       gneiss::ipc::envelope& output) {
  if (!valid_source(value) ||
      (operation != ipc_scene_operation::load && operation != ipc_scene_operation::cancel) ||
      (operation == ipc_scene_operation::load ? request_id == 0U : request_id != 0U)) {
    return gneiss::error::invalid_argument;
  }
  gneiss::ipc::envelope envelope{
      .domain = gneiss::protocol::domain::scene,
      .operation = static_cast<std::uint16_t>(operation),
      .kind = operation == ipc_scene_operation::load ? gneiss::ipc::message_kind::request
                                                     : gneiss::ipc::message_kind::event,
      .request_id = request_id,
      .payload = {},
  };
  auto encoded = protocol::encode_payload(value, ipc_scene_max_payload_size);
  if (!encoded)
    return encoded.error().error;
  envelope.payload = std::move(*encoded);
  output = std::move(envelope);
  return error::success;
}
gneiss::error decode_ipc_scene_request(const gneiss::ipc::envelope& envelope,
                                       ipc_scene_request& output) {
  const auto operation = static_cast<ipc_scene_operation>(envelope.operation);
  if (envelope.domain != gneiss::protocol::domain::scene ||
      envelope.payload.size() > ipc_scene_max_payload_size ||
      gneiss::ipc::validate_envelope(envelope) != gneiss::error::success ||
      (operation == ipc_scene_operation::load
           ? envelope.kind != gneiss::ipc::message_kind::request
           : operation != ipc_scene_operation::cancel ||
                 envelope.kind != gneiss::ipc::message_kind::event)) {
    return gneiss::error::invalid_argument;
  }
  auto parsed =
      protocol::decode_payload<ipc_scene_request>(envelope.payload, ipc_scene_max_payload_size);
  if (!parsed)
    return parsed.error().error;
  if (!valid_source(*parsed))
    return error::invalid_argument;
  output = std::move(*parsed);
  return error::success;
}
gneiss::error encode_ipc_scene_progress(const ipc_scene_progress& value, std::uint32_t request_id,
                                        gneiss::ipc::envelope& output) {
  if (!valid_progress(value)) {
    return gneiss::error::invalid_argument;
  }
  auto payload = protocol::encode_payload(value, ipc_scene_max_payload_size);
  if (!payload)
    return payload.error().error;
  const bool response = request_id != 0U && scene_phase_terminal(value.phase);
  gneiss::ipc::envelope envelope{
      .domain = gneiss::protocol::domain::scene,
      .operation = static_cast<std::uint16_t>(response ? ipc_scene_operation::load
                                                       : ipc_scene_operation::progress),
      .kind = response ? gneiss::ipc::message_kind::response : gneiss::ipc::message_kind::event,
      .request_id = response ? request_id : 0U,
      .payload = {},
  };
  envelope.payload = std::move(*payload);
  output = std::move(envelope);
  return gneiss::error::success;
}
gneiss::error decode_ipc_scene_progress(const gneiss::ipc::envelope& envelope,
                                        ipc_scene_progress& output) {
  const bool response =
      envelope.operation == 1U && envelope.kind == gneiss::ipc::message_kind::response;
  if (envelope.domain != gneiss::protocol::domain::scene ||
      envelope.payload.size() > ipc_scene_max_payload_size ||
      gneiss::ipc::validate_envelope(envelope) != gneiss::error::success ||
      (!response &&
       (envelope.operation != 3U || envelope.kind != gneiss::ipc::message_kind::event))) {
    return gneiss::error::invalid_argument;
  }
  auto value =
      protocol::decode_payload<ipc_scene_progress>(envelope.payload, ipc_scene_max_payload_size);
  if (!value)
    return value.error().error;
  if (!valid_progress(*value) || (response && !scene_phase_terminal(value->phase)))
    return error::invalid_argument;
  output = std::move(*value);
  return error::success;
}
} // namespace gneiss::host
