// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_asset_protocol.h"

int main() {
  const gneiss::ipc_asset_reload_request request{
      .session_id = 7U,
      .revision = 11U,
      .assets = {{.uri = "asset://imported/a/texture-0.texture.json",
                  .type = gneiss::ipc_asset_type::texture},
                 {.uri = "asset://imported/a/mesh-0.gneiss-mesh",
                  .type = gneiss::ipc_asset_type::static_mesh}}};
  gneiss::ipc_envelope envelope;
  gneiss::ipc_asset_reload_request decoded_request;
  if (gneiss::encode_ipc_asset_request_v2(request, gneiss::ipc_asset_operation::reload, 19U,
                                          envelope) != gneiss::result::success ||
      gneiss::decode_ipc_asset_request_v2(envelope, decoded_request) != gneiss::result::success ||
      decoded_request.revision != request.revision || decoded_request.assets.size() != 2U ||
      envelope.request_id != 19U) {
    return 1;
  }
  auto invalid = request;
  invalid.assets.push_back(invalid.assets.front());
  if (gneiss::encode_ipc_asset_request_v2(invalid, gneiss::ipc_asset_operation::reload, 20U,
                                          envelope) != gneiss::result::invalid_argument) {
    return 2;
  }
  if (gneiss::encode_ipc_asset_request_v2(request, gneiss::ipc_asset_operation::resync, 21U,
                                          envelope) != gneiss::result::success ||
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
                                         envelope) != gneiss::result::success ||
      gneiss::decode_ipc_asset_result_v2(envelope, decoded_response) != gneiss::result::success ||
      decoded_response.status != response.status || decoded_response.message != response.message) {
    return 4;
  }
  envelope.kind = gneiss::ipc_message_kind::event;
  return gneiss::decode_ipc_asset_result_v2(envelope, decoded_response) ==
                     gneiss::result::invalid_argument &&
                 gneiss::ipc_asset_operations().size() == 2U
             ? 0
             : 5;
}
