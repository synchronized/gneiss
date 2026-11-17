// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/protocol/registration.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/protocol/domains.hpp>
#include "runtime/ipc/editor_ipc_event.hpp"
#include <gneiss/engine/error.hpp>

using namespace gneiss::host;

int main() {
  if (gneiss::initialize_meta(gneiss::protocol::register_types).failed()) return 98;
  gneiss::ipc::envelope envelope;
  if (gneiss::host::encode_ipc_control_state(gneiss::host::ipc_control_state::paused, envelope) !=
      gneiss::error::success) {
    return 1;
  }
  gneiss::host::runtime_ipc_event event;
  if (gneiss::host::decode_runtime_control_event(envelope, event) !=
          gneiss::error::success ||
      !std::holds_alternative<gneiss::host::runtime_state_event>(event) ||
      std::get<gneiss::host::runtime_state_event>(event).value !=
          gneiss::host::ipc_control_state::paused) {
    return 2;
  }
  auto encoded_log = gneiss::protocol::encode(log_record, runtime_log_record{.source = "runtime", .category = "test", .message = "测试日志"});
  if (!encoded_log) return 3;
  envelope = std::move(*encoded_log);
  if (decode_runtime_log_event(envelope, event).failed() ||
      !std::holds_alternative<runtime_log_event>(event) ||
      std::get<runtime_log_event>(event).value.message != "测试日志") return 3;
  const gneiss::host::ipc_asset_reload_result asset_result{
      .session_id = 9U,
      .revision = 4U,
      .status = gneiss::host::ipc_asset_apply_status::applied,
      .message = "已应用"};
  auto encoded_asset =
      gneiss::protocol::encode(gneiss::host::asset_reload_result, asset_result, 8U);
  if (!encoded_asset)
    return 4;
  envelope = std::move(*encoded_asset);
  if (gneiss::host::decode_runtime_asset_event(envelope, event) != gneiss::error::success ||
      !std::holds_alternative<gneiss::host::runtime_asset_result_event>(event) ||
      std::get<gneiss::host::runtime_asset_result_event>(event).value.revision != 4U) {
    return 4;
  }
  envelope.domain = gneiss::protocol::domain::property;
  return gneiss::host::decode_runtime_property_event(envelope, event) != gneiss::error::success ? 0
                                                                                                : 5;
}
