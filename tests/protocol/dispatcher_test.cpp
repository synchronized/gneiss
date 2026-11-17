// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/dispatcher.hpp>

#include <array>
#include <cstdint>

namespace {

struct handler_state final {
  std::size_t call_count = 0U;
  gneiss::error result = gneiss::error::success;
};

gneiss::error handle(void* context, const gneiss::ipc::envelope& envelope) noexcept {
  (void)envelope;
  auto& state = *static_cast<handler_state*>(context);
  ++state.call_count;
  return state.result;
}

[[nodiscard]] gneiss::ipc::envelope make_control_request() {
  return {.domain = gneiss::protocol::domain::control,
          .operation = 1U,
          .kind = gneiss::ipc::message_kind::request,
          .request_id = 1U,
          .payload = {}};
}

[[nodiscard]] bool test_registration() {
  handler_state state;
  constexpr std::array operations{gneiss::protocol::operation_descriptor{
      .operation = 1U,
      .editor_to_runtime_kinds = gneiss::protocol::kind_mask(gneiss::ipc::message_kind::request),
      .runtime_to_editor_kinds = 0U}};
  const gneiss::protocol::domain_descriptor descriptor{
      .domain = gneiss::protocol::domain::control,
      .version = 1U,
      .capability = "control_v1",
      .max_payload_size = 32U,
      .operations = operations,
      .handler = handle,
      .handler_context = &state,
  };
  gneiss::protocol::dispatcher registry;
  if (registry.register_domain(descriptor) != gneiss::error::success || registry.size() != 1U ||
      registry.register_domain(descriptor) != gneiss::error::invalid_state) {
    return false;
  }
  auto invalid_operations = operations;
  invalid_operations[0].operation = 0U;
  auto invalid = descriptor;
  invalid.domain = gneiss::protocol::domain::property;
  invalid.operations = invalid_operations;
  if (registry.register_domain(invalid) != gneiss::error::invalid_argument) {
    return false;
  }
  constexpr std::array duplicate_operations{
      gneiss::protocol::operation_descriptor{.operation = 1U, .editor_to_runtime_kinds = 1U},
      gneiss::protocol::operation_descriptor{.operation = 1U, .editor_to_runtime_kinds = 1U}};
  invalid.operations = duplicate_operations;
  if (registry.register_domain(invalid) != gneiss::error::invalid_argument) {
    return false;
  }
  registry.clear();
  return registry.size() == 0U;
}

[[nodiscard]] bool test_dispatch_guards_and_handler() {
  handler_state state;
  constexpr std::array operations{gneiss::protocol::operation_descriptor{
      .operation = 1U,
      .editor_to_runtime_kinds = gneiss::protocol::kind_mask(gneiss::ipc::message_kind::request),
      .runtime_to_editor_kinds = 0U}};
  gneiss::protocol::dispatcher registry;
  if (registry.register_domain({.domain = gneiss::protocol::domain::control,
                                .version = 2U,
                                .capability = "control_v2",
                                .max_payload_size = 2U,
                                .operations = operations,
                                .handler = handle,
                                .handler_context = &state}) != gneiss::error::success) {
    return false;
  }
  const auto& dispatcher = registry;
  auto envelope = make_control_request();
  gneiss::protocol::dispatch_context context{.remote_role = gneiss::protocol::peer_role::editor,
                                             .handshake_complete = false,
                                             .negotiated_domains = {}};
  envelope.request_id = 0U;
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::protocol::dispatch_rejection::invalid_envelope) {
    return false;
  }
  envelope.request_id = 1U;
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::protocol::dispatch_rejection::handshake_required) {
    return false;
  }
  context.handshake_complete = true;
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::protocol::dispatch_rejection::domain_not_negotiated) {
    return false;
  }
  constexpr std::array old_capability{gneiss::protocol::domain_capability{
      .domain = gneiss::protocol::domain::control, .version = 1U}};
  context.negotiated_domains = old_capability;
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::protocol::dispatch_rejection::unsupported_domain_version) {
    return false;
  }
  constexpr std::array capability{gneiss::protocol::domain_capability{
      .domain = gneiss::protocol::domain::control, .version = 2U}};
  context.negotiated_domains = capability;
  context.remote_role = gneiss::protocol::peer_role::runtime;
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::protocol::dispatch_rejection::wrong_direction) {
    return false;
  }
  context.remote_role = gneiss::protocol::peer_role::editor;
  envelope.kind = gneiss::ipc::message_kind::response;
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::protocol::dispatch_rejection::wrong_kind) {
    return false;
  }
  envelope.kind = gneiss::ipc::message_kind::request;
  envelope.operation = 9U;
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::protocol::dispatch_rejection::unknown_operation) {
    return false;
  }
  envelope.operation = 1U;
  envelope.payload = {1U, 2U, 3U};
  if (dispatcher.dispatch(envelope, context).rejection !=
      gneiss::protocol::dispatch_rejection::payload_too_large) {
    return false;
  }
  envelope.payload.clear();
  if (!dispatcher.dispatch(envelope, context).accepted() || state.call_count != 1U) {
    return false;
  }
  state.result = gneiss::error::not_ready;
  const auto failed = dispatcher.dispatch(envelope, context);
  return failed.rejection == gneiss::protocol::dispatch_rejection::handler_failed &&
         failed.handler_result == gneiss::error::not_ready && state.call_count == 2U;
}

[[nodiscard]] bool test_session_and_unknown_domain() {
  handler_state state;
  constexpr std::array operations{gneiss::protocol::operation_descriptor{
      .operation = 1U,
      .editor_to_runtime_kinds = gneiss::protocol::kind_mask(gneiss::ipc::message_kind::response),
      .runtime_to_editor_kinds = gneiss::protocol::kind_mask(gneiss::ipc::message_kind::request)}};
  gneiss::protocol::dispatcher registry;
  if (registry.register_domain({.domain = gneiss::protocol::domain::session,
                                .version = 1U,
                                .capability = {},
                                .max_payload_size = 16U,
                                .operations = operations,
                                .handler = handle,
                                .handler_context = &state}) != gneiss::error::success) {
    return false;
  }
  const auto& dispatcher = registry;
  gneiss::ipc::envelope envelope{.domain = gneiss::protocol::domain::session,
                                 .operation = 1U,
                                 .kind = gneiss::ipc::message_kind::request,
                                 .request_id = 1U,
                                 .payload = {}};
  if (!dispatcher.dispatch(envelope, {}).accepted() || state.call_count != 1U) {
    return false;
  }
  envelope.domain = static_cast<std::uint16_t>(99U);
  return dispatcher.dispatch(envelope, {}).rejection ==
         gneiss::protocol::dispatch_rejection::unknown_domain;
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
