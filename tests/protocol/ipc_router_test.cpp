// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/protocol/ipc_control_protocol.hpp>
#include <gneiss/protocol/ipc_router.hpp>
#include <gneiss/protocol/ipc_scene_protocol.hpp>
#include <gneiss/protocol/registration.hpp>

#include <array>

using namespace gneiss::host;

namespace {

struct routed_message final {
  gneiss::host::ipc_control_operation operation = gneiss::host::ipc_control_operation::invalid;
};

struct operation_context final {
  std::uint32_t request_id = 0U;
};

gneiss::error decode_control(const gneiss::ipc::envelope& envelope,
                             routed_message& output) noexcept {
  return gneiss::host::decode_ipc_control_request(envelope, output.operation);
}

gneiss::error handle_pause(const gneiss::ipc::envelope& envelope,
                           operation_context& context) noexcept {
  context.request_id = envelope.request_id;
  return gneiss::error::success;
}

bool test_binding_and_dispatch() {
  gneiss::host::ipc_router<routed_message> router;
  if (router.bind(gneiss::protocol::domain::control, decode_control) != gneiss::error::success ||
      router.bind(gneiss::protocol::domain::control, decode_control) !=
          gneiss::error::invalid_argument) {
    return false;
  }
  gneiss::ipc::envelope envelope;
  if (gneiss::host::encode_ipc_control_request(gneiss::host::ipc_control_operation::pause, 7U,
                                               envelope) != gneiss::error::success) {
    return false;
  }
  constexpr std::array domains{
      gneiss::protocol::domain_capability{.domain = gneiss::protocol::domain::control,
                                          .version = gneiss::host::ipc_control_domain_version}};
  const gneiss::protocol::dispatch_context before_handshake{.remote_role =
                                                                gneiss::protocol::peer_role::editor,
                                                            .handshake_complete = false,
                                                            .negotiated_domains = domains};
  if (router.dispatch(envelope, before_handshake).outcome.rejection !=
      gneiss::protocol::dispatch_rejection::handshake_required) {
    return false;
  }
  const gneiss::protocol::dispatch_context connected{.remote_role =
                                                         gneiss::protocol::peer_role::editor,
                                                     .handshake_complete = true,
                                                     .negotiated_domains = domains};
  auto routed = router.dispatch(envelope, connected);
  return routed.accepted() &&
         routed.message->operation == gneiss::host::ipc_control_operation::pause;
}

bool test_operation_binding_and_dispatch() {
  gneiss::host::ipc_operation_router<operation_context> router;
  if (router.bind(gneiss::protocol::domain::control,
                  static_cast<std::uint16_t>(gneiss::host::ipc_control_operation::pause),
                  handle_pause) != gneiss::error::success ||
      router.bind(gneiss::protocol::domain::control,
                  static_cast<std::uint16_t>(gneiss::host::ipc_control_operation::pause),
                  handle_pause) != gneiss::error::invalid_argument) {
    return false;
  }
  gneiss::ipc::envelope envelope;
  if (gneiss::host::encode_ipc_control_request(gneiss::host::ipc_control_operation::pause, 27U,
                                               envelope) != gneiss::error::success) {
    return false;
  }
  constexpr std::array domains{
      gneiss::protocol::domain_capability{.domain = gneiss::protocol::domain::control,
                                          .version = gneiss::host::ipc_control_domain_version}};
  const gneiss::protocol::dispatch_context connected{.remote_role =
                                                         gneiss::protocol::peer_role::editor,
                                                     .handshake_complete = true,
                                                     .negotiated_domains = domains};
  operation_context context;
  const auto routed = router.dispatch(envelope, connected, context);
  return routed.accepted() && context.request_id == 27U;
}

bool test_typed_scene_routes() {
  using namespace gneiss;
  using namespace gneiss::host;
  ipc_operation_router<int> router;
  auto load = [](int& count, protocol::request_context request, ipc_scene_request value) {
    if (request.id != 23 || value.session != 1)
      return error::invalid_argument;
    ++count;
    return error::success;
  };
  if (router.bind(scene_load, load).failed() || router.bind(scene_load, load).ok())
    return false;
  if (router
          .bind(scene_result,
                [](int& count, protocol::request_context request, ipc_scene_progress value) {
                  if (request.id != 23 || value.phase != ipc_scene_phase::applied)
                    return error::invalid_argument;
                  ++count;
                  return error::success;
                })
          .failed())
    return false;
  const std::array domains{
      protocol::domain_capability{protocol::domain::scene, ipc_scene_domain_version}};
  protocol::dispatch_context context{protocol::peer_role::editor, true, domains};
  const ipc_scene_request source{1, 2, "asset://scene.json"};
  auto request = protocol::encode(scene_load, source, 23);
  int count = 0;
  if (!request || !router.dispatch(*request, context, count).accepted() || count != 1)
    return false;
  ipc_scene_progress progress{.source = source, .phase = ipc_scene_phase::applied, .message = {}};
  auto response = protocol::encode(scene_result, progress, 23);
  context.remote_role = protocol::peer_role::runtime;
  return response && router.dispatch(*response, context, count).accepted() && count == 2;
}

} // namespace

int main() {
  if (gneiss::initialize_meta(gneiss::protocol::register_types).failed())
    return 2;
  return test_typed_scene_routes() && test_binding_and_dispatch() &&
                 test_operation_binding_and_dispatch()
             ? 0
             : 1;
}
