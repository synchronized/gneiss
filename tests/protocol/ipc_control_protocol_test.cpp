// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/protocol/ipc_control_protocol.hpp>
#include <gneiss/protocol/registration.hpp>

#include <string_view>

using namespace gneiss::host;

namespace {

[[nodiscard]] bool test_runtime_events() {
  gneiss::ipc::envelope envelope;
  if (gneiss::host::encode_ipc_control_ready(envelope) != gneiss::error::success ||
      envelope.domain != gneiss::protocol::domain::control ||
      envelope.operation !=
          static_cast<std::uint16_t>(gneiss::host::ipc_control_operation::ready) ||
      envelope.kind != gneiss::ipc::message_kind::event || !envelope.payload.empty()) {
    return false;
  }
  auto frame = gneiss::protocol::encode(control_state_changed,
                                        ipc_control_state_message{ipc_control_state::paused});
  if (!frame)
    return false;
  auto state = gneiss::protocol::decode(control_state_changed, *frame);
  return state && state->state == ipc_control_state::paused;
}

[[nodiscard]] bool test_editor_requests() {
  for (const auto expected :
       {gneiss::host::ipc_control_operation::pause, gneiss::host::ipc_control_operation::resume,
        gneiss::host::ipc_control_operation::stop}) {
    gneiss::ipc::envelope envelope;
    if (gneiss::host::encode_ipc_control_request(expected, 7U, envelope) !=
        gneiss::error::success) {
      return false;
    }
    gneiss::host::ipc_control_operation decoded{};
    if (gneiss::host::decode_ipc_control_request(envelope, decoded) != gneiss::error::success ||
        decoded != expected) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool test_rejections() {
  gneiss::ipc::envelope envelope;
  if (gneiss::host::encode_ipc_control_request(gneiss::host::ipc_control_operation::ready, 1U,
                                               envelope) != gneiss::error::invalid_argument ||
      gneiss::host::encode_ipc_control_request(gneiss::host::ipc_control_operation::pause, 0U,
                                               envelope) != gneiss::error::invalid_argument ||
      gneiss::protocol::encode(control_state_changed,
                               ipc_control_state_message{static_cast<ipc_control_state>(99U)})) {
    return false;
  }
  auto frame = gneiss::protocol::encode(control_state_changed,
                                        ipc_control_state_message{ipc_control_state::running});
  if (!frame)
    return false;
  envelope = std::move(*frame);
  for (const std::string_view json :
       {"{}", "{", "{\"state\":99}", "{\"state\":-1}", "{\"state\":\"paused\"}"}) {
    envelope.payload.assign(json.begin(), json.end());
    if (gneiss::protocol::decode(control_state_changed, envelope))
      return false;
  }
  envelope.payload.assign(ipc_control_max_payload_size + 1, ' ');
  if (gneiss::protocol::decode(control_state_changed, envelope))
    return false;
  if (encode_ipc_control_request(ipc_control_operation::pause, 3, envelope).failed())
    return false;
  envelope.payload = {'{', '}'};
  auto operation = ipc_control_operation::invalid;
  return decode_ipc_control_request(envelope, operation).failed() &&
         operation == ipc_control_operation::invalid;
}

[[nodiscard]] bool test_direction_rules() {
  const auto operations = gneiss::host::ipc_control_operations();
  return operations.size() == 5U && operations[0].editor_to_runtime_kinds == 0U &&
         operations[0].runtime_to_editor_kinds ==
             gneiss::protocol::kind_mask(gneiss::ipc::message_kind::event) &&
         operations[2].editor_to_runtime_kinds ==
             gneiss::protocol::kind_mask(gneiss::ipc::message_kind::request) &&
         operations[2].runtime_to_editor_kinds == 0U;
}

} // namespace

int main() {
  if (gneiss::initialize_meta(gneiss::protocol::register_types).failed())
    return 98;
  try {
    return test_runtime_events() && test_editor_requests() && test_rejections() &&
                   test_direction_rules()
               ? 0
               : 1;
  } catch (...) {
    return 1;
  }
}
