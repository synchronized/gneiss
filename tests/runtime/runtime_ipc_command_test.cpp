// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc/runtime_commands.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/protocol/registration.hpp>

#include <gneiss/protocol/ipc_asset_protocol.hpp>
#include <gneiss/protocol/ipc_inspection_protocol.hpp>
#include <gneiss/protocol/ipc_property_protocol.hpp>
#include <gneiss/protocol/ipc_session_protocol.hpp>

#include <array>
#include <vector>

namespace {

struct sent_envelopes final {
  std::vector<gneiss::ipc::envelope> values;
};

gneiss::error capture_send(void* context, gneiss::ipc::envelope envelope) noexcept {
  try {
    static_cast<sent_envelopes*>(context)->values.push_back(std::move(envelope));
    return gneiss::error::success;
  } catch (...) {
    return gneiss::error::out_of_memory;
  }
}

} // namespace

int main() {
  if (gneiss::initialize_meta(gneiss::protocol::register_types).failed())
    return 98;
  using namespace gneiss;
  using namespace gneiss::host;

  runtime_command_router router;
  if (register_runtime_commands(router) != gneiss::error::success) {
    return 1;
  }
  constexpr std::array domains{
      gneiss::protocol::domain_capability{.domain = gneiss::protocol::domain::session,
                                          .version = ipc_session_domain_version},
      gneiss::protocol::domain_capability{.domain = gneiss::protocol::domain::control,
                                          .version = gneiss::host::ipc_control_domain_version},
      gneiss::protocol::domain_capability{.domain = gneiss::protocol::domain::inspection,
                                          .version = 1U},
      gneiss::protocol::domain_capability{.domain = gneiss::protocol::domain::property,
                                          .version = ipc_property_domain_version},
      gneiss::protocol::domain_capability{.domain = gneiss::protocol::domain::asset,
                                          .version = ipc_asset_domain_version}};
  const gneiss::protocol::dispatch_context dispatch_context{.remote_role =
                                                                gneiss::protocol::peer_role::editor,
                                                            .handshake_complete = true,
                                                            .negotiated_domains = domains};
  runtime_ipc_state state = runtime_ipc_state::running;
  runtime_ipc_actions actions;
  sent_envelopes sent;
  runtime_command_context context(state, actions, &sent, capture_send);

  gneiss::ipc::envelope envelope;
  if (encode_ipc_control_request(ipc_control_operation::pause, 7U, envelope) !=
          gneiss::error::success ||
      !router.dispatch(envelope, dispatch_context, context).accepted() || !actions.pause_game ||
      state != runtime_ipc_state::paused || sent.values.empty()) {
    return 2;
  }
  ipc_control_state reported{};
  if (decode_ipc_control_state(sent.values.back(), reported).failed() ||
      reported != ipc_control_state::paused)
    return 12;
  actions.pause_game = false;
  if (!router.dispatch(envelope, dispatch_context, context).accepted() || actions.pause_game ||
      state != runtime_ipc_state::paused || sent.values.back().kind != ipc::message_kind::error)
    return 13;
  auto old_domains = domains;
  old_domains[1].version = 1U;
  auto old_context = dispatch_context;
  old_context.negotiated_domains = old_domains;
  if (router.dispatch(envelope, old_context, context).rejection !=
      protocol::dispatch_rejection::unsupported_domain_version)
    return 14;
  const ipc_property_write property{.session_id = 3U,
                                    .command_id = 9U,
                                    .object = {4U, 2U},
                                    .type_id = gneiss::guid::from_bytes({1U}),
                                    .field_id = 5U,
                                    .expected_revision = 6U,
                                    .value = {true}};
  auto encoded_property = protocol::encode(property_write, property, 9U);
  if (!encoded_property)
    return 3;
  envelope = std::move(*encoded_property);
  if (!router.dispatch(envelope, dispatch_context, context).accepted() ||
      actions.property_writes.size() != 1U || actions.property_writes.front().command_id != 9U) {
    return 3;
  }
  const ipc_asset_reload_request assets{.session_id = 3U,
                                        .revision = 4U,
                                        .assets = {{.uri = "asset://imported/a/mesh-0.gneiss-mesh",
                                                    .type = ipc_asset_type::static_mesh}}};
  auto encoded_asset = protocol::encode(asset_reload, assets, 10U);
  if (!encoded_asset)
    return 4;
  envelope = std::move(*encoded_asset);
  if (!router.dispatch(envelope, dispatch_context, context).accepted() ||
      actions.asset_reloads.size() != 1U || actions.asset_reloads.front().request_id != 10U) {
    return 4;
  }
  auto encoded_cancel = protocol::encode(asset_cancel, ipc_asset_cancel_request{3, 4});
  if (!encoded_cancel)
    return 5;
  envelope = std::move(*encoded_cancel);
  if (!router.dispatch(envelope, dispatch_context, context).accepted() ||
      actions.asset_cancels.size() != 1U || actions.asset_cancels.front().session_id != 3U ||
      actions.asset_cancels.front().revision != 4U)
    return 5;
  envelope.kind = gneiss::ipc::message_kind::request;
  if (router.dispatch(envelope, dispatch_context, context).accepted())
    return 6;
  const ipc_scene_request scene{3U, 5U, "asset://scenes/main.scene.json"};
  auto load_message = protocol::encode(scene_load, scene, 11U);
  if (!load_message)
    return 7;
  envelope = std::move(*load_message);
  if (router.dispatch(envelope, dispatch_context, context).rejection !=
      gneiss::protocol::dispatch_rejection::domain_not_negotiated)
    return 7;
  std::array scene_domains{gneiss::protocol::domain_capability{
      .domain = gneiss::protocol::domain::scene, .version = ipc_scene_domain_version}};
  auto scene_dispatch = dispatch_context;
  scene_dispatch.negotiated_domains = scene_domains;
  if (!router.dispatch(envelope, scene_dispatch, context).accepted() ||
      actions.scene_commands.size() != 1U || actions.scene_commands.front().cancel ||
      actions.scene_commands.front().request_id != 11U ||
      actions.scene_commands.front().request.uri != scene.uri)
    return 8;
  auto cancel_message = protocol::encode(scene_cancel, scene);
  if (!cancel_message)
    return 9;
  envelope = std::move(*cancel_message);
  if (!router.dispatch(envelope, scene_dispatch, context).accepted() ||
      actions.scene_commands.size() != 2U || !actions.scene_commands.back().cancel)
    return 9;
  // 通用分发器把能力版本视为可理解的最高版本，不能把更高版本误当作不兼容。
  scene_domains.front().version = 99U;
  if (!router.dispatch(envelope, scene_dispatch, context).accepted())
    return 10;
  scene_domains.front().version = 0U;
  if (router.dispatch(envelope, scene_dispatch, context).rejection !=
      gneiss::protocol::dispatch_rejection::unsupported_domain_version)
    return 11;
  scene_domains.front().version = ipc_scene_domain_version;
  // 结构错误和业务数据错误不得入队，队满仍返回背压。
  const auto count = actions.scene_commands.size();
  envelope.payload = {'{', '}'};
  if (router.dispatch(envelope, scene_dispatch, context).accepted() ||
      actions.scene_commands.size() != count)
    return 15;
  auto encoded_scene = protocol::encode(scene_load, scene, 12U);
  if (!encoded_scene)
    return 16;
  envelope = std::move(*encoded_scene);
  auto wrong_sender = scene_dispatch;
  wrong_sender.remote_role = protocol::peer_role::runtime;
  if (router.dispatch(envelope, wrong_sender, context).accepted() ||
      actions.scene_commands.size() != count)
    return 17;
  actions.scene_commands.resize(8);
  if (router.dispatch(envelope, scene_dispatch, context).handler_result != error::not_ready ||
      actions.scene_commands.size() != 8)
    return 18;
  auto resync_message = protocol::encode(asset_resync, assets, 20U);
  if (!resync_message || !router.dispatch(*resync_message, dispatch_context, context).accepted() ||
      actions.asset_reloads.back().operation != ipc_asset_operation::resync)
    return 19;
  const auto asset_count = actions.asset_reloads.size();
  resync_message->payload = {'{', '}'};
  if (router.dispatch(*resync_message, dispatch_context, context).accepted() ||
      actions.asset_reloads.size() != asset_count)
    return 20;
  auto reload_message = protocol::encode(asset_reload, assets, 21U);
  if (!reload_message)
    return 21;
  actions.asset_reloads.resize(8);
  if (router.dispatch(*reload_message, dispatch_context, context).handler_result !=
          error::not_ready ||
      actions.asset_reloads.size() != 8)
    return 22;
  actions.asset_cancels.resize(8);
  auto cancel_asset = protocol::encode(asset_cancel, ipc_asset_cancel_request{3, 4});
  if (!cancel_asset ||
      router.dispatch(*cancel_asset, dispatch_context, context).handler_result !=
          error::not_ready ||
      actions.asset_cancels.size() != 8)
    return 23;
  return 0;
}
