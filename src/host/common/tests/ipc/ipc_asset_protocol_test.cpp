// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "ipc_asset_protocol.hpp"

int main() {
  static_assert(gneiss::ipc_asset_domain_version == 4U);
  const gneiss::ipc_asset_reload_request request{
      .session_id = 7U,
      .revision = 11U,
      .assets = {{.uri = "asset://imported/a/texture-0.texture.json",
                  .type = gneiss::ipc_asset_type::texture},
                 {.uri = "asset://imported/a/mesh-0.gneiss-mesh",
                  .type = gneiss::ipc_asset_type::static_mesh}}};
  gneiss::engine::platform::ipc_envelope envelope;
  gneiss::ipc_asset_reload_request decoded_request;
  if (gneiss::encode_ipc_asset_request_v2(request, gneiss::ipc_asset_operation::reload, 19U,
                                          envelope) != gneiss::engine::core::result::success ||
      gneiss::decode_ipc_asset_request_v2(envelope, decoded_request) !=
          gneiss::engine::core::result::success ||
      decoded_request.revision != request.revision || decoded_request.assets.size() != 2U ||
      envelope.request_id != 19U) {
    return 1;
  }
  auto invalid = request;
  invalid.assets.push_back(invalid.assets.front());
  if (gneiss::encode_ipc_asset_request_v2(invalid, gneiss::ipc_asset_operation::reload, 20U,
                                          envelope) !=
      gneiss::engine::core::result::invalid_argument) {
    return 2;
  }
  invalid = request;
  invalid.assets.push_back(
      {.uri = "asset://scenes/main.scene.json", .type = gneiss::ipc_asset_type::scene});
  if (gneiss::encode_ipc_asset_request_v2(invalid, gneiss::ipc_asset_operation::reload, 20U,
                                          envelope) !=
      gneiss::engine::core::result::invalid_argument) {
    return 2;
  }
  const gneiss::ipc_asset_reload_request prefab_request{
      .session_id = 7U,
      .revision = 12U,
      .assets = {
          {.uri = "asset://prefabs/lamp.prefab.json", .type = gneiss::ipc_asset_type::prefab}}};
  if (gneiss::encode_ipc_asset_request_v2(prefab_request, gneiss::ipc_asset_operation::reload, 20U,
                                          envelope) != gneiss::engine::core::result::success ||
      gneiss::decode_ipc_asset_request_v2(envelope, decoded_request) !=
          gneiss::engine::core::result::success ||
      decoded_request.assets.front().type != gneiss::ipc_asset_type::prefab) {
    return 2;
  }
  if (gneiss::encode_ipc_asset_request_v2(request, gneiss::ipc_asset_operation::resync, 21U,
                                          envelope) != gneiss::engine::core::result::success ||
      envelope.operation != static_cast<std::uint16_t>(gneiss::ipc_asset_operation::resync)) {
    return 3;
  }

  const gneiss::ipc_asset_reload_result response{
      .session_id = 7U,
      .revision = 11U,
      .status = gneiss::ipc_asset_apply_status::restart_required,
      .message = "当前资源类型不能安全替换"};
  gneiss::ipc_asset_reload_result decoded_response;
  if (gneiss::encode_ipc_asset_result_v2(response, gneiss::ipc_asset_operation::reload, 19U,
                                         envelope) != gneiss::engine::core::result::success ||
      gneiss::decode_ipc_asset_result_v2(envelope, decoded_response) !=
          gneiss::engine::core::result::success ||
      decoded_response.status != response.status || decoded_response.message != response.message) {
    return 4;
  }
  auto partial = response;
  partial.status = gneiss::ipc_asset_apply_status::failed;
  partial.completed_assets = 1U;
  partial.total_assets = 2U;
  partial.items = {{request.assets[0], gneiss::engine::core::result::success, true},
                   {request.assets[1], gneiss::engine::core::result::io, false}};
  if (gneiss::encode_ipc_asset_result(partial, envelope.payload) !=
          gneiss::engine::core::result::success ||
      gneiss::decode_ipc_asset_result(envelope.payload, decoded_response) !=
          gneiss::engine::core::result::success ||
      decoded_response.items.size() != 2U || !decoded_response.items[0].applied ||
      decoded_response.items[1].applied ||
      decoded_response.items[1].operation != gneiss::engine::core::result::io) {
    return 12;
  }
  partial.items[1].applied = true;
  if (gneiss::encode_ipc_asset_result(partial, envelope.payload) !=
      gneiss::engine::core::result::invalid_argument) {
    return 13;
  }
  auto progress = response;
  progress.status = gneiss::ipc_asset_apply_status::preparing;
  progress.can_cancel = true;
  progress.total_assets = 7U;
  progress.completed_assets = 2U;
  if (gneiss::encode_ipc_asset_result_v2(progress, gneiss::ipc_asset_operation::progress, 0U,
                                         envelope) != gneiss::engine::core::result::success ||
      gneiss::decode_ipc_asset_result_v2(envelope, decoded_response) !=
          gneiss::engine::core::result::success ||
      !decoded_response.can_cancel || decoded_response.completed_assets != 2U)
    return 6;
  envelope.request_id = 1U;
  if (gneiss::decode_ipc_asset_result_v2(envelope, decoded_response) !=
      gneiss::engine::core::result::invalid_argument)
    return 7;
  progress.status = gneiss::ipc_asset_apply_status::uploading;
  if (gneiss::encode_ipc_asset_result_v2(progress, gneiss::ipc_asset_operation::progress, 0U,
                                         envelope) !=
      gneiss::engine::core::result::invalid_argument)
    return 8;
  if (gneiss::encode_ipc_asset_cancel(7U, 11U, envelope) != gneiss::engine::core::result::success ||
      gneiss::decode_ipc_asset_cancel(envelope, decoded_request) !=
          gneiss::engine::core::result::success ||
      decoded_request.revision != 11U)
    return 9;
  envelope.request_id = 1U;
  if (gneiss::decode_ipc_asset_cancel(envelope, decoded_request) !=
      gneiss::engine::core::result::invalid_argument)
    return 10;
  if (gneiss::encode_ipc_asset_result_v2(response, gneiss::ipc_asset_operation::reload, 19U,
                                         envelope) != gneiss::engine::core::result::success)
    return 11;
  envelope.kind = gneiss::engine::platform::ipc_message_kind::event;
  return gneiss::decode_ipc_asset_result_v2(envelope, decoded_response) ==
                     gneiss::engine::core::result::invalid_argument &&
                 gneiss::ipc_asset_operations().size() == 4U
             ? 0
             : 5;
}
