// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/protocol/ipc_session_protocol.hpp>
#include <gneiss/protocol/registration.hpp>

#include <array>

using namespace gneiss::host;

namespace {

[[nodiscard]] bool test_hello_and_negotiation() {
  const gneiss::host::ipc_session_hello request{
      .token = "secret",
      .domains = {{.domain = gneiss::protocol::domain::control, .version = 2U},
                  {.domain = gneiss::protocol::domain::property, .version = 1U}}};
  auto envelope = gneiss::protocol::encode(session_hello, request, 7U);
  if (!envelope)
    return false;
  auto decoded = gneiss::protocol::decode(session_hello, *envelope);
  if (!decoded || decoded->token != request.token || decoded->domains.size() != 2U)
    return false;
  constexpr std::array supported{
      gneiss::protocol::domain_capability{gneiss::protocol::domain::control, 1U},
      gneiss::protocol::domain_capability{gneiss::protocol::domain::log, 1U}};
  ipc_session_hello acknowledgment;
  if (negotiate_ipc_session_hello(*decoded, "secret", supported, acknowledgment).failed() ||
      acknowledgment.domains.size() != 1U || acknowledgment.domains[0].version != 1U)
    return false;
  auto response = gneiss::protocol::encode(session_accepted, acknowledgment, envelope->request_id);
  if (!response)
    return false;
  auto accepted = gneiss::protocol::decode(session_accepted, *response);
  return accepted && accepted->token.empty() && accepted->domains == acknowledgment.domains;
}

[[nodiscard]] bool test_session_messages() {
  namespace p = gneiss::protocol;
  auto ping = p::encode(session_ping, ipc_session_heartbeat{42U}, 8U);
  if (!ping)
    return false;
  auto heartbeat = p::decode(session_ping, *ping);
  if (!heartbeat || heartbeat->nonce != 42U)
    return false;
  auto failure = p::encode(session_runtime_error, ipc_session_error{-8, "状态错误"}, 8U);
  if (!failure)
    return false;
  auto decoded_error = p::decode(session_runtime_error, *failure);
  if (!decoded_error || decoded_error->code != -8 || decoded_error->message != "状态错误")
    return false;
  auto shutdown = p::encode(session_shutdown, ipc_session_shutdown{3});
  if (!shutdown)
    return false;
  auto decoded_shutdown = p::decode(session_shutdown, *shutdown);
  return decoded_shutdown && decoded_shutdown->exit_code == 3;
}

[[nodiscard]] bool test_rejections() {
  gneiss::host::ipc_session_hello request{
      .token = "secret", .domains = {{.domain = gneiss::protocol::domain::control, .version = 1U}}};
  gneiss::host::ipc_session_hello output;
  constexpr std::array supported{gneiss::protocol::domain_capability{
      .domain = gneiss::protocol::domain::control, .version = 1U}};
  if (gneiss::host::negotiate_ipc_session_hello(request, "wrong", supported, output) !=
      gneiss::error::invalid_argument) {
    return false;
  }
  request.domains.push_back(request.domains.front());
  if (gneiss::host::negotiate_ipc_session_hello(request, "secret", supported, output) !=
      gneiss::error::invalid_argument) {
    return false;
  }
  return !gneiss::protocol::encode(session_hello, request, 0U);
}

[[nodiscard]] bool test_direction_rules() {
  const auto operations = gneiss::host::ipc_session_operations();
  return operations.size() == 4U &&
         operations[0].runtime_to_editor_kinds ==
             gneiss::protocol::kind_mask(gneiss::ipc::message_kind::request) &&
         operations[0].editor_to_runtime_kinds ==
             gneiss::protocol::kind_mask(gneiss::ipc::message_kind::response) &&
         operations[3].editor_to_runtime_kinds == 0U;
}

[[nodiscard]] bool test_typed_validation() {
  namespace p = gneiss::protocol;
  ipc_session_hello request{.token = "secret", .domains = {{1, 2}}};
  if (p::encode(session_accepted, request, 1))
    return false;
  auto encoded = p::encode(session_hello, request, 1);
  if (!encoded)
    return false;
  auto malformed = *encoded;
  malformed.payload = {'{', '}'};
  if (p::decode(session_hello, malformed))
    return false;
  malformed.payload.resize(ipc_session_max_payload_size + 1);
  if (p::decode(session_hello, malformed))
    return false;
  request.token.assign(257, 'x');
  if (p::encode(session_hello, request, 1))
    return false;
  request.token = "secret";
  request.domains = {{0, 1}};
  if (p::encode(session_hello, request, 1))
    return false;
  request.domains = {{1, 0}};
  if (p::encode(session_hello, request, 1))
    return false;
  ipc_session_hello accepted{.token = "", .domains = {{1, 2}}};
  auto response = p::encode(session_accepted, accepted, 1);
  if (!response)
    return false;
  const std::string text(response->payload.begin(), response->payload.end());
  if (text.find("secret") != std::string::npos || p::decode(session_hello, *response))
    return false;
  auto ping = p::encode(session_ping, ipc_session_heartbeat{42}, 1);
  if (!ping || p::decode(session_pong, *ping))
    return false;
  for (const std::string payload : {"{\"nonce\":-1}", "{\"nonce\":1.5}"}) {
    ping->payload.assign(payload.begin(), payload.end());
    if (p::decode(session_ping, *ping))
      return false;
  }
  return true;
}

[[nodiscard]] bool test_timeout_tracker() {
  using namespace std::chrono_literals;
  const auto start = gneiss::host::ipc_timeout_tracker::clock::time_point{};
  gneiss::host::ipc_timeout_tracker tracker(3s);
  tracker.reset(start);
  return !tracker.expired(start + 2999ms) && tracker.expired(start + 3s) &&
         gneiss::host::ipc_timeout_tracker(0s).expired(start);
}

} // namespace

int main() {
  if (gneiss::initialize_meta(gneiss::protocol::register_types).failed())
    return 98;
  try {
    if (!test_hello_and_negotiation()) {
      return 1;
    }
    if (!test_session_messages()) {
      return 2;
    }
    return test_typed_validation() && test_rejections() && test_direction_rules() &&
                   test_timeout_tracker()
               ? 0
               : 3;
  } catch (...) {
    return 1;
  }
}
