// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_ipc_event.h"

int main() {
  gneiss::ipc_envelope envelope;
  if (gneiss::encode_ipc_control_state(gneiss::ipc_control_state::paused, envelope) !=
      gneiss::result::success) {
    return 1;
  }
  gneiss::editor::runtime_ipc_event event;
  if (gneiss::editor::decode_runtime_control_event(envelope, event) != gneiss::result::success ||
      !std::holds_alternative<gneiss::editor::runtime_state_event>(event) ||
      std::get<gneiss::editor::runtime_state_event>(event).value !=
          gneiss::ipc_control_state::paused) {
    return 2;
  }
  if (gneiss::encode_ipc_log_event("测试日志", envelope) != gneiss::result::success ||
      gneiss::editor::decode_runtime_log_event(envelope, event) != gneiss::result::success ||
      !std::holds_alternative<gneiss::editor::runtime_log_event>(event) ||
      std::get<gneiss::editor::runtime_log_event>(event).value != "测试日志") {
    return 3;
  }
  envelope.domain = gneiss::ipc_domain::property;
  return gneiss::editor::decode_runtime_property_event(envelope, event) != gneiss::result::success
             ? 0
             : 4;
}
