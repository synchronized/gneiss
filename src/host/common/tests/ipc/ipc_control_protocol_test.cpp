// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "ipc_control_protocol.hpp"

namespace {

[[nodiscard]] bool test_runtime_events() {
  gneiss::engine::platform::ipc_envelope envelope;
  if (gneiss::encode_ipc_control_ready(envelope) != gneiss::engine::core::result::success ||
      envelope.domain != gneiss::engine::platform::ipc_domain::control ||
      envelope.operation != static_cast<std::uint16_t>(gneiss::ipc_control_operation::ready) ||
      envelope.kind != gneiss::engine::platform::ipc_message_kind::event ||
      !envelope.payload.empty()) {
    return false;
  }
  if (gneiss::encode_ipc_control_state(gneiss::ipc_control_state::paused, envelope) !=
      gneiss::engine::core::result::success) {
    return false;
  }
  gneiss::ipc_control_state state{};
  return gneiss::decode_ipc_control_state(envelope, state) ==
             gneiss::engine::core::result::success &&
         state == gneiss::ipc_control_state::paused;
}

[[nodiscard]] bool test_editor_requests() {
  for (const auto expected :
       {gneiss::ipc_control_operation::pause, gneiss::ipc_control_operation::resume,
        gneiss::ipc_control_operation::stop}) {
    gneiss::engine::platform::ipc_envelope envelope;
    if (gneiss::encode_ipc_control_request(expected, 7U, envelope) !=
        gneiss::engine::core::result::success) {
      return false;
    }
    gneiss::ipc_control_operation decoded{};
    if (gneiss::decode_ipc_control_request(envelope, decoded) !=
            gneiss::engine::core::result::success ||
        decoded != expected) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool test_rejections() {
  gneiss::engine::platform::ipc_envelope envelope;
  if (gneiss::encode_ipc_control_request(gneiss::ipc_control_operation::ready, 1U, envelope) !=
          gneiss::engine::core::result::invalid_argument ||
      gneiss::encode_ipc_control_request(gneiss::ipc_control_operation::pause, 0U, envelope) !=
          gneiss::engine::core::result::invalid_argument ||
      gneiss::encode_ipc_control_state(static_cast<gneiss::ipc_control_state>(99U), envelope) !=
          gneiss::engine::core::result::invalid_argument) {
    return false;
  }
  if (gneiss::encode_ipc_control_state(gneiss::ipc_control_state::running, envelope) !=
      gneiss::engine::core::result::success) {
    return false;
  }
  envelope.payload.front() = 99U;
  gneiss::ipc_control_state state{};
  return gneiss::decode_ipc_control_state(envelope, state) ==
         gneiss::engine::core::result::invalid_argument;
}

[[nodiscard]] bool test_direction_rules() {
  const auto operations = gneiss::ipc_control_operations();
  return operations.size() == 5U && operations[0].editor_to_runtime_kinds == 0U &&
         operations[0].runtime_to_editor_kinds ==
             gneiss::engine::platform::ipc_kind_mask(
                 gneiss::engine::platform::ipc_message_kind::event) &&
         operations[2].editor_to_runtime_kinds ==
             gneiss::engine::platform::ipc_kind_mask(
                 gneiss::engine::platform::ipc_message_kind::request) &&
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
