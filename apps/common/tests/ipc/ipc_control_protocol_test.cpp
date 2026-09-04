// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_control_protocol.h"

namespace {

[[nodiscard]] bool test_runtime_events() {
  gneiss::ipc_envelope envelope;
  if (gneiss::encode_ipc_control_ready(envelope) != gneiss::result::success ||
      envelope.domain != gneiss::ipc_domain::control ||
      envelope.operation != static_cast<std::uint16_t>(gneiss::ipc_control_operation::ready) ||
      envelope.kind != gneiss::ipc_message_kind::event || !envelope.payload.empty()) {
    return false;
  }
  if (gneiss::encode_ipc_control_state(gneiss::ipc_control_state::paused, envelope) !=
      gneiss::result::success) {
    return false;
  }
  gneiss::ipc_control_state state{};
  return gneiss::decode_ipc_control_state(envelope, state) == gneiss::result::success &&
         state == gneiss::ipc_control_state::paused;
}

[[nodiscard]] bool test_editor_requests() {
  for (const auto expected :
       {gneiss::ipc_control_operation::pause, gneiss::ipc_control_operation::resume,
        gneiss::ipc_control_operation::stop}) {
    gneiss::ipc_envelope envelope;
    if (gneiss::encode_ipc_control_request(expected, 7U, envelope) != gneiss::result::success) {
      return false;
    }
    gneiss::ipc_control_operation decoded{};
    if (gneiss::decode_ipc_control_request(envelope, decoded) != gneiss::result::success ||
        decoded != expected) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool test_rejections() {
  gneiss::ipc_envelope envelope;
  if (gneiss::encode_ipc_control_request(gneiss::ipc_control_operation::ready, 1U, envelope) !=
          gneiss::result::invalid_argument ||
      gneiss::encode_ipc_control_request(gneiss::ipc_control_operation::pause, 0U, envelope) !=
          gneiss::result::invalid_argument ||
      gneiss::encode_ipc_control_state(static_cast<gneiss::ipc_control_state>(99U), envelope) !=
          gneiss::result::invalid_argument) {
    return false;
  }
  if (gneiss::encode_ipc_control_state(gneiss::ipc_control_state::running, envelope) !=
      gneiss::result::success) {
    return false;
  }
  envelope.payload.front() = 99U;
  gneiss::ipc_control_state state{};
  return gneiss::decode_ipc_control_state(envelope, state) == gneiss::result::invalid_argument;
}

[[nodiscard]] bool test_direction_rules() {
  const auto operations = gneiss::ipc_control_operations();
  return operations.size() == 5U && operations[0].editor_to_runtime_kinds == 0U &&
         operations[0].runtime_to_editor_kinds ==
             gneiss::ipc_kind_mask(gneiss::ipc_message_kind::event) &&
         operations[2].editor_to_runtime_kinds ==
             gneiss::ipc_kind_mask(gneiss::ipc_message_kind::request) &&
         operations[2].runtime_to_editor_kinds == 0U;
}

} // namespace

int main() {
  try {
    return test_runtime_events() && test_editor_requests() && test_rejections() &&
                   test_direction_rules()
               ? 0
               : 1;
  } catch (...) {
    return 1;
  }
}
