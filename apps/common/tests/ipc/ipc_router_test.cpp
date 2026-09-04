// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_control_protocol.h"
#include "ipc_router.h"

#include <array>

namespace {

struct routed_message final {
  gneiss::ipc_control_operation operation = gneiss::ipc_control_operation::invalid;
};

struct operation_context final {
  std::uint32_t request_id = 0U;
};

gneiss::result decode_control(const gneiss::ipc_envelope& envelope,
                              routed_message& output) noexcept {
  return gneiss::decode_ipc_control_request(envelope, output.operation);
}

gneiss::result handle_pause(const gneiss::ipc_envelope& envelope,
                            operation_context& context) noexcept {
  context.request_id = envelope.request_id;
  return gneiss::result::success;
}

bool test_binding_and_dispatch() {
  gneiss::ipc_router<routed_message> router;
  if (router.bind(gneiss::ipc_domain::control, decode_control) != gneiss::result::success ||
      router.bind(gneiss::ipc_domain::control, decode_control) !=
          gneiss::result::invalid_argument) {
    return false;
  }
  gneiss::ipc_envelope envelope;
  if (gneiss::encode_ipc_control_request(gneiss::ipc_control_operation::pause, 7U, envelope) !=
      gneiss::result::success) {
    return false;
  }
  constexpr std::array domains{
      gneiss::ipc_domain_capability{.domain = gneiss::ipc_domain::control, .version = 1U}};
  const gneiss::ipc_dispatch_context before_handshake{.remote_role = gneiss::ipc_peer_role::editor,
                                                      .handshake_complete = false,
                                                      .negotiated_domains = domains};
  if (router.dispatch(envelope, before_handshake).outcome.rejection !=
      gneiss::ipc_dispatch_rejection::handshake_required) {
    return false;
  }
  const gneiss::ipc_dispatch_context connected{.remote_role = gneiss::ipc_peer_role::editor,
                                               .handshake_complete = true,
                                               .negotiated_domains = domains};
  auto routed = router.dispatch(envelope, connected);
  return routed.accepted() && routed.message->operation == gneiss::ipc_control_operation::pause;
}

bool test_operation_binding_and_dispatch() {
  gneiss::ipc_operation_router<operation_context> router;
  if (router.bind(gneiss::ipc_domain::control,
                  static_cast<std::uint16_t>(gneiss::ipc_control_operation::pause),
                  handle_pause) != gneiss::result::success ||
      router.bind(gneiss::ipc_domain::control,
                  static_cast<std::uint16_t>(gneiss::ipc_control_operation::pause),
                  handle_pause) != gneiss::result::invalid_argument) {
    return false;
  }
  gneiss::ipc_envelope envelope;
  if (gneiss::encode_ipc_control_request(gneiss::ipc_control_operation::pause, 27U, envelope) !=
      gneiss::result::success) {
    return false;
  }
  constexpr std::array domains{
      gneiss::ipc_domain_capability{.domain = gneiss::ipc_domain::control, .version = 1U}};
  const gneiss::ipc_dispatch_context connected{.remote_role = gneiss::ipc_peer_role::editor,
                                               .handshake_complete = true,
                                               .negotiated_domains = domains};
  operation_context context;
  const auto routed = router.dispatch(envelope, connected, context);
  return routed.accepted() && context.request_id == 27U;
}

} // namespace

int main() { return test_binding_and_dispatch() && test_operation_binding_and_dispatch() ? 0 : 1; }
