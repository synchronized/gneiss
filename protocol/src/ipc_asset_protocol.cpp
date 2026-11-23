// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <algorithm>
#include <array>
#include <gneiss/protocol/ipc_asset_protocol.hpp>
#include <set>
namespace gneiss::host {
namespace {
[[nodiscard]] bool valid_asset_group(std::span<const ipc_asset_revision> assets) noexcept {
  if (assets.empty()) {
    return false;
  }
  const bool structural =
      assets.front().type == ipc_asset_type::scene || assets.front().type == ipc_asset_type::prefab;
  return std::ranges::all_of(assets, [&](const auto& asset) {
    if (!structural) {
      return asset.type == ipc_asset_type::texture || asset.type == ipc_asset_type::material ||
             asset.type == ipc_asset_type::static_mesh;
    }
    return asset.type == assets.front().type;
  });
}

bool valid_uri(std::string_view uri) {
  return uri.starts_with("asset://") && uri.size() <= 2048 &&
         uri.find("..") == std::string_view::npos;
}
bool valid_request(const ipc_asset_reload_request& v) {
  if (!v.session_id || !v.revision || v.assets.size() > 1024 || !valid_asset_group(v.assets))
    return false;
  std::set<std::string_view> uris;
  for (const auto& a : v.assets)
    if (!valid_uri(a.uri) || a.type > ipc_asset_type::prefab || !uris.insert(a.uri).second)
      return false;
  return true;
}
bool valid_result(const ipc_asset_reload_result& v) {
  if (!v.session_id || !v.revision || v.status > ipc_asset_apply_status::cancelled ||
      v.message.size() > 4096 || v.completed_assets > v.total_assets || v.total_assets > 256 ||
      v.items.size() > 256 || (v.can_cancel && v.status != ipc_asset_apply_status::preparing))
    return false;
  std::set<std::string_view> uris;
  for (const auto& i : v.items)
    if (!valid_uri(i.asset.uri) || i.asset.type > ipc_asset_type::prefab ||
        !uris.insert(i.asset.uri).second || (i.applied && i.operation.failed()) ||
        i.operation.code() > error::success.code() || i.operation.code() < error::io.code())
      return false;
  return true;
}
bool progressing(const ipc_asset_reload_result& v) {
  return v.status == ipc_asset_apply_status::preparing ||
         v.status == ipc_asset_apply_status::uploading;
}
bool valid_terminal(const ipc_asset_reload_result& v) { return valid_result(v) && !progressing(v); }
}
const protocol::message_descriptor<ipc_asset_reload_request> asset_reload{
    protocol::domain::asset,    static_cast<std::uint16_t>(ipc_asset_operation::reload),
    ipc::message_kind::request, protocol::peer_role::editor,
    ipc_asset_max_payload_size, valid_request};
const protocol::message_descriptor<ipc_asset_reload_request> asset_resync{
    protocol::domain::asset,    static_cast<std::uint16_t>(ipc_asset_operation::resync),
    ipc::message_kind::request, protocol::peer_role::editor,
    ipc_asset_max_payload_size, valid_request};
const protocol::message_descriptor<ipc_asset_cancel_request> asset_cancel{
    protocol::domain::asset,
    static_cast<std::uint16_t>(ipc_asset_operation::cancel),
    ipc::message_kind::event,
    protocol::peer_role::editor,
    256U,
    [](const ipc_asset_cancel_request& v) { return v.session_id != 0 && v.revision != 0; }};
const protocol::message_descriptor<ipc_asset_reload_result> asset_reload_result{
    protocol::domain::asset,     static_cast<std::uint16_t>(ipc_asset_operation::reload),
    ipc::message_kind::response, protocol::peer_role::runtime,
    ipc_asset_max_payload_size,  valid_terminal};
const protocol::message_descriptor<ipc_asset_reload_result> asset_resync_result{
    protocol::domain::asset,     static_cast<std::uint16_t>(ipc_asset_operation::resync),
    ipc::message_kind::response, protocol::peer_role::runtime,
    ipc_asset_max_payload_size,  valid_terminal};
const protocol::message_descriptor<ipc_asset_reload_result> asset_progress{
    protocol::domain::asset,
    static_cast<std::uint16_t>(ipc_asset_operation::progress),
    ipc::message_kind::event,
    protocol::peer_role::runtime,
    ipc_asset_max_payload_size,
    [](const ipc_asset_reload_result& v) { return valid_result(v) && progressing(v); }};
std::span<const protocol::operation_descriptor> ipc_asset_operations() noexcept {
  static const std::array operations = [] {
    auto reload = asset_reload.rule();
    reload.runtime_to_editor_kinds |= asset_reload_result.rule().runtime_to_editor_kinds;
    auto resync = asset_resync.rule();
    resync.runtime_to_editor_kinds |= asset_resync_result.rule().runtime_to_editor_kinds;
    return std::array{asset_progress.rule(), asset_cancel.rule(), reload, resync};
  }();
  return operations;
}
}
