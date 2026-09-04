// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_session_protocol.h"

#include <array>

namespace {

[[nodiscard]] bool test_hello_and_negotiation() {
  const gneiss::ipc_session_hello request{
      .token = "secret",
      .domains = {{.domain = gneiss::ipc_domain::control, .version = 2U},
                  {.domain = gneiss::ipc_domain::property, .version = 1U}}};
  gneiss::ipc_envelope envelope;
  if (gneiss::encode_ipc_session_hello(request, false, 7U, envelope) != gneiss::result::success) {
    return false;
  }
  gneiss::ipc_session_hello decoded;
  if (gneiss::decode_ipc_session_hello(envelope, decoded) != gneiss::result::success ||
      decoded.token != request.token || decoded.domains.size() != 2U) {
    return false;
  }
  constexpr std::array supported{
      gneiss::ipc_domain_capability{.domain = gneiss::ipc_domain::control, .version = 1U},
      gneiss::ipc_domain_capability{.domain = gneiss::ipc_domain::log, .version = 1U}};
  gneiss::ipc_session_hello acknowledgment;
  if (gneiss::negotiate_ipc_session_hello(decoded, "secret", supported, acknowledgment) !=
          gneiss::result::success ||
      acknowledgment.domains.size() != 1U || acknowledgment.domains[0].version != 1U ||
      gneiss::encode_ipc_session_hello(acknowledgment, true, envelope.request_id, envelope) !=
          gneiss::result::success) {
    return false;
  }
  gneiss::ipc_session_hello accepted;
  return gneiss::decode_ipc_session_hello(envelope, accepted) == gneiss::result::success &&
         accepted.token.empty() && accepted.domains == acknowledgment.domains;
}

[[nodiscard]] bool test_session_messages() {
  gneiss::ipc_envelope envelope;
  const gneiss::ipc_session_heartbeat heartbeat{.nonce = 42U};
  if (gneiss::encode_ipc_session_heartbeat(heartbeat, false, 8U, envelope) !=
      gneiss::result::success) {
    return false;
  }
  gneiss::ipc_session_heartbeat decoded_heartbeat;
  if (gneiss::decode_ipc_session_heartbeat(envelope, decoded_heartbeat) !=
          gneiss::result::success ||
      decoded_heartbeat.nonce != heartbeat.nonce) {
    return false;
  }
  const gneiss::ipc_session_error error{.code = -8, .message = "状态错误"};
  if (gneiss::encode_ipc_session_error(error, 8U, envelope) != gneiss::result::success) {
    return false;
  }
  gneiss::ipc_session_error decoded_error;
  if (gneiss::decode_ipc_session_error(envelope, decoded_error) != gneiss::result::success ||
      decoded_error.code != error.code || decoded_error.message != error.message) {
    return false;
  }
  const gneiss::ipc_session_shutdown shutdown{.exit_code = 3};
  if (gneiss::encode_ipc_session_shutdown(shutdown, envelope) != gneiss::result::success) {
    return false;
  }
  gneiss::ipc_session_shutdown decoded_shutdown;
  return gneiss::decode_ipc_session_shutdown(envelope, decoded_shutdown) ==
             gneiss::result::success &&
         decoded_shutdown.exit_code == shutdown.exit_code;
}

[[nodiscard]] bool test_rejections() {
  gneiss::ipc_session_hello request{
      .token = "secret", .domains = {{.domain = gneiss::ipc_domain::control, .version = 1U}}};
  gneiss::ipc_session_hello output;
  constexpr std::array supported{
      gneiss::ipc_domain_capability{.domain = gneiss::ipc_domain::control, .version = 1U}};
  if (gneiss::negotiate_ipc_session_hello(request, "wrong", supported, output) !=
      gneiss::result::invalid_argument) {
    return false;
  }
  request.domains.push_back(request.domains.front());
  if (gneiss::negotiate_ipc_session_hello(request, "secret", supported, output) !=
      gneiss::result::invalid_argument) {
    return false;
  }
  gneiss::ipc_envelope envelope;
  return gneiss::encode_ipc_session_hello(request, false, 0U, envelope) ==
         gneiss::result::invalid_argument;
}

[[nodiscard]] bool test_direction_rules() {
  const auto operations = gneiss::ipc_session_operations();
  return operations.size() == 4U &&
         operations[0].runtime_to_editor_kinds ==
             gneiss::ipc_kind_mask(gneiss::ipc_message_kind::request) &&
         operations[0].editor_to_runtime_kinds ==
             gneiss::ipc_kind_mask(gneiss::ipc_message_kind::response) &&
         operations[3].editor_to_runtime_kinds == 0U;
}

[[nodiscard]] bool test_timeout_tracker() {
  using namespace std::chrono_literals;
  const auto start = gneiss::ipc_timeout_tracker::clock::time_point{};
  gneiss::ipc_timeout_tracker tracker(3s);
  tracker.reset(start);
  return !tracker.expired(start + 2999ms) && tracker.expired(start + 3s) &&
         gneiss::ipc_timeout_tracker(0s).expired(start);
}

} // namespace

int main() {
  try {
    if (!test_hello_and_negotiation()) {
      return 1;
    }
    if (!test_session_messages()) {
      return 2;
    }
    return test_rejections() && test_direction_rules() && test_timeout_tracker() ? 0 : 3;
  } catch (...) {
    return 1;
  }
}
