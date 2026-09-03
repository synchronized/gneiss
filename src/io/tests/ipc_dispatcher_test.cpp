// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_dispatcher.h"

#include <array>
#include <cstdint>

namespace {

struct handler_state final {
  std::size_t call_count = 0U;
  gneiss::result result = gneiss::result::success;
};

gneiss::result handle(void* context, const gneiss::ipc_envelope& envelope) noexcept {
  (void)envelope;
  auto& state = *static_cast<handler_state*>(context);
  ++state.call_count;
  return state.result;
}

[[nodiscard]] gneiss::ipc_envelope make_control_request() {
  return {.domain = gneiss::ipc_domain::control,
          .operation = 1U,
          .kind = gneiss::ipc_message_kind::request,
          .request_id = 1U,
          .payload = {}};
}

[[nodiscard]] bool test_registration() {
  handler_state state;
  constexpr std::array operations{gneiss::ipc_operation_descriptor{
      .operation = 1U, .allowed_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::request)}};
  const gneiss::ipc_domain_descriptor descriptor{
      .domain = gneiss::ipc_domain::control,
      .version = 1U,
      .capability = "control_v1",
      .max_payload_size = 32U,
      .operations = operations,
      .handler = handle,
      .handler_context = &state,
  };
  gneiss::ipc_domain_registry registry;
  if (registry.register_domain(descriptor) != gneiss::result::success || registry.size() != 1U ||
      registry.register_domain(descriptor) != gneiss::result::invalid_state) {
    return false;
  }
  auto invalid_operations = operations;
  invalid_operations[0].operation = 0U;
  auto invalid = descriptor;
  invalid.domain = gneiss::ipc_domain::property;
  invalid.operations = invalid_operations;
  if (registry.register_domain(invalid) != gneiss::result::invalid_argument) {
    return false;
  }
  constexpr std::array duplicate_operations{
      gneiss::ipc_operation_descriptor{.operation = 1U, .allowed_kinds = 1U},
      gneiss::ipc_operation_descriptor{.operation = 1U, .allowed_kinds = 1U}};
  invalid.operations = duplicate_operations;
  if (registry.register_domain(invalid) != gneiss::result::invalid_argument) {
    return false;
  }
  registry.clear();
  return registry.size() == 0U;
}

[[nodiscard]] bool test_dispatch_guards_and_handler() {
  handler_state state;
  constexpr std::array operations{gneiss::ipc_operation_descriptor{
      .operation = 1U,
      .allowed_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::request),
      .direction = gneiss::ipc_message_direction::editor_to_runtime}};
  gneiss::ipc_domain_registry registry;
  if (registry.register_domain({.domain = gneiss::ipc_domain::control,
                                .version = 2U,
                                .capability = "control_v2",
                                .max_payload_size = 2U,
                                .operations = operations,
                                .handler = handle,
                                .handler_context = &state}) != gneiss::result::success) {
    return false;
  }
  const gneiss::ipc_dispatcher dispatcher(registry);
  auto envelope = make_control_request();
  gneiss::ipc_dispatch_context context{.remote_role = gneiss::ipc_peer_role::editor,
                                       .handshake_complete = false,
                                       .negotiated_domains = {}};
  envelope.request_id = 0U;
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::ipc_dispatch_rejection::invalid_envelope) {
    return false;
  }
  envelope.request_id = 1U;
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::ipc_dispatch_rejection::handshake_required) {
    return false;
  }
  context.handshake_complete = true;
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::ipc_dispatch_rejection::domain_not_negotiated) {
    return false;
  }
  constexpr std::array old_capability{
      gneiss::ipc_domain_capability{.domain = gneiss::ipc_domain::control, .version = 1U}};
  context.negotiated_domains = old_capability;
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::ipc_dispatch_rejection::unsupported_domain_version) {
    return false;
  }
  constexpr std::array capability{
      gneiss::ipc_domain_capability{.domain = gneiss::ipc_domain::control, .version = 2U}};
  context.negotiated_domains = capability;
  context.remote_role = gneiss::ipc_peer_role::runtime;
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::ipc_dispatch_rejection::wrong_direction) {
    return false;
  }
  context.remote_role = gneiss::ipc_peer_role::editor;
  envelope.kind = gneiss::ipc_message_kind::response;
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::ipc_dispatch_rejection::wrong_kind) {
    return false;
  }
  envelope.kind = gneiss::ipc_message_kind::request;
  envelope.operation = 9U;
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::ipc_dispatch_rejection::unknown_operation) {
    return false;
  }
  envelope.operation = 1U;
  envelope.payload = {1U, 2U, 3U};
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::ipc_dispatch_rejection::payload_too_large) {
    return false;
  }
  envelope.payload.clear();
  if (!dispatcher.dispatch(envelope, context).accepted() || state.call_count != 1U) {
    return false;
  }
  state.result = gneiss::result::not_ready;
  const auto failed = dispatcher.dispatch(envelope, context);
  return failed.rejection == gneiss::ipc_dispatch_rejection::handler_failed &&
         failed.handler_result == gneiss::result::not_ready && state.call_count == 2U;
}

[[nodiscard]] bool test_session_and_unknown_domain() {
  handler_state state;
  constexpr std::array operations{gneiss::ipc_operation_descriptor{
      .operation = 1U, .allowed_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::request)}};
  gneiss::ipc_domain_registry registry;
  if (registry.register_domain({.domain = gneiss::ipc_domain::session,
                                .version = 1U,
                                .capability = {},
                                .max_payload_size = 16U,
                                .operations = operations,
                                .handler = handle,
                                .handler_context = &state}) != gneiss::result::success) {
    return false;
  }
  const gneiss::ipc_dispatcher dispatcher(registry);
  gneiss::ipc_envelope envelope{.domain = gneiss::ipc_domain::session,
                                .operation = 1U,
                                .kind = gneiss::ipc_message_kind::request,
                                .request_id = 1U,
                                .payload = {}};
  if (!dispatcher.dispatch(envelope, {}).accepted() || state.call_count != 1U) {
    return false;
  }
  envelope.domain = static_cast<gneiss::ipc_domain>(99U);
  return dispatcher.dispatch(envelope, {}).rejection ==
         gneiss::ipc_dispatch_rejection::unknown_domain;
}

} // namespace

int main() {
  try {
    return test_registration() && test_dispatch_guards_and_handler() &&
                   test_session_and_unknown_domain()
               ? 0
               : 1;
  } catch (...) {
    return 1;
  }
}
