// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <cstdio>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/protocol/ipc_asset_protocol.hpp>
#include <gneiss/protocol/registration.hpp>
#include <source_location>
#include <stdexcept>
using namespace gneiss;
using namespace gneiss::host;
void check(bool v, std::source_location loc = std::source_location::current()) {
  if (!v)
    throw std::runtime_error("资产协议验证失败，行=" + std::to_string(loc.line()));
}
int main() try {
  check(initialize_meta(protocol::register_types).ok());
  const ipc_asset_reload_request request{7,
                                         11,
                                         {{"asset://a.texture", ipc_asset_type::texture},
                                          {"asset://b.mesh", ipc_asset_type::static_mesh}}};
  for (const auto* message : {&asset_reload, &asset_resync}) {
    auto encoded = protocol::encode(*message, request, 19);
    check(bool(encoded));
    auto value = protocol::decode(*message, *encoded);
    check(value && value->session_id == 7 && value->revision == 11 && value->assets.size() == 2 &&
          value->assets[1].type == ipc_asset_type::static_mesh);
    check(!protocol::encode(*message, request));
    for (const std::string_view text :
         {"{}", R"({"session_id":7,"revision":11,"assets":[]})",
          R"({"session_id":7,"revision":11,"assets":[{"uri":"asset://a","type":"unknown"}]})"}) {
      encoded->payload.assign(text.begin(), text.end());
      check(!protocol::decode(*message, *encoded));
    }
  }
  auto invalid = request;
  invalid.assets.push_back(invalid.assets.front());
  check(!protocol::encode(asset_reload, invalid, 19));
  invalid = request;
  invalid.assets.front().uri = "asset://../escape";
  check(!protocol::encode(asset_reload, invalid, 19));
  invalid = request;
  invalid.assets.front().type = ipc_asset_type::prefab;
  check(!protocol::encode(asset_reload, invalid, 19));
  invalid.assets.resize(1);
  check(bool(protocol::encode(asset_reload, invalid, 19)));
  invalid = request;
  invalid.assets.clear();
  for (unsigned i = 0; i < 1025; ++i)
    invalid.assets.push_back({"asset://" + std::to_string(i), ipc_asset_type::texture});
  check(!protocol::encode(asset_reload, invalid, 19));
  auto cancel = protocol::encode(asset_cancel, ipc_asset_cancel_request{7, 11});
  check(cancel && protocol::decode(asset_cancel, *cancel));
  cancel->request_id = 1;
  check(!protocol::decode(asset_cancel, *cancel));
  cancel->request_id = 0;
  cancel->payload.resize(257);
  check(!protocol::decode(asset_cancel, *cancel));
  ipc_asset_reload_result response{
      .session_id = 7,
      .revision = 11,
      .status = ipc_asset_apply_status::failed,
      .message = "部分失败",
      .completed_assets = 1,
      .total_assets = 2,
      .items = {{request.assets[0], error::success, true}, {request.assets[1], error::io, false}}};
  for (const auto* message : {&asset_reload_result, &asset_resync_result}) {
    auto encoded = protocol::encode(*message, response, 19);
    check(bool(encoded));
    auto value = protocol::decode(*message, *encoded);
    check(value && value->items.size() == 2 && value->items[0].applied &&
          !value->items[1].applied && value->items[1].operation == error::io &&
          value->message == response.message);
    const std::string original(encoded->payload.begin(), encoded->payload.end());
    for (const auto replacement : {"1", "-999", "null", "1.5"}) {
      auto text = original;
      const auto marker = text.find("\"operation\":0");
      check(marker != std::string::npos);
      text.replace(marker + 12, 1, replacement);
      encoded->payload.assign(text.begin(), text.end());
      check(!protocol::decode(*message, *encoded));
    }
    encoded->kind = ipc::message_kind::event;
    check(!protocol::decode(*message, *encoded));
  }
  response.items[1].applied = true;
  check(!protocol::encode(asset_reload_result, response, 19));
  response.items.clear();
  response.status = ipc_asset_apply_status::preparing;
  response.can_cancel = true;
  auto progress = protocol::encode(asset_progress, response);
  check(progress && protocol::decode(asset_progress, *progress));
  check(!protocol::encode(asset_reload_result, response, 19));
  response.status = ipc_asset_apply_status::uploading;
  check(!protocol::encode(asset_progress, response));
  response.can_cancel = false;
  check(bool(protocol::encode(asset_progress, response)));
  response.status = ipc_asset_apply_status::cancelled;
  check(!protocol::encode(asset_progress, response));
  check(bool(protocol::encode(asset_reload_result, response, 19)));
  response.total_assets = 257;
  check(!protocol::encode(asset_reload_result, response, 19));
  check(ipc_asset_operations().size() == 4);
  return 0;
} catch (const std::exception& e) {
  std::fprintf(stderr, "%s\n", e.what());
  return 1;
}
