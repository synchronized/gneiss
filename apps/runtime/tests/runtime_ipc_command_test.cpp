// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_command.h"

int main() {
  gneiss::ipc_envelope envelope;
  if (gneiss::encode_ipc_control_request(gneiss::ipc_control_operation::pause, 7U, envelope) !=
      gneiss::result::success) {
    return 1;
  }
  gneiss::runtime_internal::runtime_ipc_command command;
  if (gneiss::runtime_internal::decode_runtime_control_command(envelope, command) !=
          gneiss::result::success ||
      command.kind != gneiss::runtime_internal::runtime_ipc_command_kind::pause ||
      command.request_id != 7U) {
    return 2;
  }
  const gneiss::ipc_property_write property{.session_id = 3U,
                                            .command_id = 9U,
                                            .object = {4U, 2U},
                                            .type_id = {{1U}},
                                            .field_id = 5U,
                                            .expected_revision = 6U,
                                            .value = {true}};
  if (gneiss::encode_ipc_property_write_v2(property, 9U, envelope) != gneiss::result::success ||
      gneiss::runtime_internal::decode_runtime_property_command(envelope, command) !=
          gneiss::result::success ||
      command.kind != gneiss::runtime_internal::runtime_ipc_command_kind::property_write ||
      command.property.command_id != 9U) {
    return 3;
  }
  return gneiss::runtime_internal::decode_runtime_control_command(envelope, command) !=
                 gneiss::result::success
             ? 0
             : 4;
}
