// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "ipc_control_protocol.hpp"

using namespace gneiss::host;

namespace {

[[nodiscard]] bool test_runtime_events() {
  gneiss::engine::ipc_envelope envelope;
  if (gneiss::host::encode_ipc_control_ready(envelope) != gneiss::engine::error::success ||
      envelope.domain != gneiss::engine::ipc_domain::control ||
      envelope.operation !=
          static_cast<std::uint16_t>(gneiss::host::ipc_control_operation::ready) ||
      envelope.kind != gneiss::engine::ipc_message_kind::event || !envelope.payload.empty()) {
    return false;
  }
  if (gneiss::host::encode_ipc_control_state(gneiss::host::ipc_control_state::paused, envelope) !=
      gneiss::engine::error::success) {
    return false;
  }
  gneiss::host::ipc_control_state state{};
  return gneiss::host::decode_ipc_control_state(envelope, state) ==
             gneiss::engine::error::success &&
         state == gneiss::host::ipc_control_state::paused;
}

[[nodiscard]] bool test_editor_requests() {
  for (const auto expected :
       {gneiss::host::ipc_control_operation::pause, gneiss::host::ipc_control_operation::resume,
        gneiss::host::ipc_control_operation::stop}) {
    gneiss::engine::ipc_envelope envelope;
    if (gneiss::host::encode_ipc_control_request(expected, 7U, envelope) !=
        gneiss::engine::error::success) {
      return false;
    }
    gneiss::host::ipc_control_operation decoded{};
    if (gneiss::host::decode_ipc_control_request(envelope, decoded) !=
            gneiss::engine::error::success ||
        decoded != expected) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool test_rejections() {
  gneiss::engine::ipc_envelope envelope;
  if (gneiss::host::encode_ipc_control_request(gneiss::host::ipc_control_operation::ready, 1U,
                                               envelope) !=
          gneiss::engine::error::invalid_argument ||
      gneiss::host::encode_ipc_control_request(gneiss::host::ipc_control_operation::pause, 0U,
                                               envelope) !=
          gneiss::engine::error::invalid_argument ||
      gneiss::host::encode_ipc_control_state(static_cast<gneiss::host::ipc_control_state>(99U),
                                             envelope) != gneiss::engine::error::invalid_argument) {
    return false;
  }
  if (gneiss::host::encode_ipc_control_state(gneiss::host::ipc_control_state::running, envelope) !=
      gneiss::engine::error::success) {
    return false;
  }
  envelope.payload.front() = 99U;
  gneiss::host::ipc_control_state state{};
  return gneiss::host::decode_ipc_control_state(envelope, state) ==
         gneiss::engine::error::invalid_argument;
}

[[nodiscard]] bool test_direction_rules() {
  const auto operations = gneiss::host::ipc_control_operations();
  return operations.size() == 5U && operations[0].editor_to_runtime_kinds == 0U &&
         operations[0].runtime_to_editor_kinds ==
             gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::event) &&
         operations[2].editor_to_runtime_kinds ==
             gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::request) &&
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
