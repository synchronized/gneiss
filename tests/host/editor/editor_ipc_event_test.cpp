// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_event.hpp"
#include "engine/core/error.hpp"

using namespace gneiss::host;

int main() {
  gneiss::engine::ipc_envelope envelope;
  if (gneiss::host::encode_ipc_control_state(gneiss::host::ipc_control_state::paused, envelope) !=
      gneiss::engine::error::success) {
    return 1;
  }
  gneiss::host::runtime_ipc_event event;
  if (gneiss::host::decode_runtime_control_event(envelope, event) !=
          gneiss::engine::error::success ||
      !std::holds_alternative<gneiss::host::runtime_state_event>(event) ||
      std::get<gneiss::host::runtime_state_event>(event).value !=
          gneiss::host::ipc_control_state::paused) {
    return 2;
  }
  if (gneiss::host::encode_ipc_log_event("测试日志", envelope) != gneiss::engine::error::success ||
      gneiss::host::decode_runtime_log_event(envelope, event) != gneiss::engine::error::success ||
      !std::holds_alternative<gneiss::host::runtime_log_event>(event) ||
      std::get<gneiss::host::runtime_log_event>(event).value != "测试日志") {
    return 3;
  }
  const gneiss::host::ipc_asset_reload_result asset_result{
      .session_id = 9U,
      .revision = 4U,
      .status = gneiss::host::ipc_asset_apply_status::applied,
      .message = "已应用"};
  if (gneiss::host::encode_ipc_asset_result_v2(asset_result,
                                               gneiss::host::ipc_asset_operation::reload, 8U,
                                               envelope) != gneiss::engine::error::success ||
      gneiss::host::decode_runtime_asset_event(envelope, event) != gneiss::engine::error::success ||
      !std::holds_alternative<gneiss::host::runtime_asset_result_event>(event) ||
      std::get<gneiss::host::runtime_asset_result_event>(event).value.revision != 4U) {
    return 4;
  }
  envelope.domain = gneiss::engine::ipc_domain::property;
  return gneiss::host::decode_runtime_property_event(envelope, event) !=
                 gneiss::engine::error::success
             ? 0
             : 5;
}
