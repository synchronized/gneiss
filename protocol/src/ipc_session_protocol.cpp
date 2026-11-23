// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <algorithm>
#include <array>
#include <gneiss/protocol/ipc_session_protocol.hpp>

namespace {
constexpr std::size_t max_token_size = 256U;
constexpr std::size_t max_domain_count = 32U;
constexpr std::size_t max_error_message_size = 16U * 1024U;
[[nodiscard]] bool constant_time_equal(std::string_view left, std::string_view right) noexcept {
  std::size_t difference = left.size() ^ right.size();
  const auto count = (std::max)(left.size(), right.size());
  for (std::size_t index = 0U; index < count; ++index) {
    const auto left_value = index < left.size() ? static_cast<unsigned char>(left[index]) : 0U;
    const auto right_value = index < right.size() ? static_cast<unsigned char>(right[index]) : 0U;
    difference |= left_value ^ right_value;
  }
  return difference == 0U;
}

[[nodiscard]] bool
valid_domains(std::span<const gneiss::protocol::domain_capability> domains) noexcept {
  if (domains.size() > max_domain_count) {
    return false;
  }
  for (std::size_t index = 0U; index < domains.size(); ++index) {
    const auto remaining = domains.subspan(index + 1U);
    if (static_cast<std::uint16_t>(domains[index].domain) == 0U || domains[index].version == 0U ||
        std::ranges::find(remaining, domains[index].domain,
                          &gneiss::protocol::domain_capability::domain) != remaining.end()) {
      return false;
    }
  }
  return true;
}

}
namespace gneiss::host {
const protocol::message_descriptor<ipc_session_hello> session_hello{
    protocol::domain::session,
    static_cast<std::uint16_t>(ipc_session_operation::hello),
    ipc::message_kind::request,
    protocol::peer_role::runtime,
    ipc_session_max_payload_size,
    [](const ipc_session_hello& v) {
      return !v.token.empty() && v.token.size() <= max_token_size && valid_domains(v.domains);
    }};
const protocol::message_descriptor<ipc_session_hello> session_accepted{
    protocol::domain::session,
    static_cast<std::uint16_t>(ipc_session_operation::hello),
    ipc::message_kind::response,
    protocol::peer_role::editor,
    ipc_session_max_payload_size,
    [](const ipc_session_hello& v) { return v.token.empty() && valid_domains(v.domains); }};
const protocol::message_descriptor<ipc_session_heartbeat> session_ping{
    protocol::domain::session,    static_cast<std::uint16_t>(ipc_session_operation::heartbeat),
    ipc::message_kind::request,   protocol::peer_role::editor,
    ipc_session_max_payload_size, nullptr};
const protocol::message_descriptor<ipc_session_heartbeat> session_pong{
    protocol::domain::session,    static_cast<std::uint16_t>(ipc_session_operation::heartbeat),
    ipc::message_kind::response,  protocol::peer_role::runtime,
    ipc_session_max_payload_size, nullptr};
const protocol::message_descriptor<ipc_session_error> session_editor_error{
    protocol::domain::session,
    static_cast<std::uint16_t>(ipc_session_operation::protocol_error),
    ipc::message_kind::error,
    protocol::peer_role::editor,
    ipc_session_max_payload_size,
    [](const ipc_session_error& v) { return v.message.size() <= max_error_message_size; }};
const protocol::message_descriptor<ipc_session_error> session_runtime_error{
    protocol::domain::session,
    static_cast<std::uint16_t>(ipc_session_operation::protocol_error),
    ipc::message_kind::error,
    protocol::peer_role::runtime,
    ipc_session_max_payload_size,
    [](const ipc_session_error& v) { return v.message.size() <= max_error_message_size; }};
const protocol::message_descriptor<ipc_session_shutdown> session_shutdown{
    protocol::domain::session,
    static_cast<std::uint16_t>(ipc_session_operation::shutdown_complete),
    ipc::message_kind::event,
    protocol::peer_role::runtime,
    ipc_session_max_payload_size,
    nullptr};
namespace {
const std::array session_operations = [] {
  auto hello = session_hello.rule();
  hello.editor_to_runtime_kinds |= session_accepted.rule().editor_to_runtime_kinds;
  auto heartbeat = session_ping.rule();
  heartbeat.runtime_to_editor_kinds |= session_pong.rule().runtime_to_editor_kinds;
  auto failure = session_editor_error.rule();
  failure.runtime_to_editor_kinds |= session_runtime_error.rule().runtime_to_editor_kinds;
  return std::array{hello, heartbeat, failure, session_shutdown.rule()};
}();
}
gneiss::error
negotiate_ipc_session_hello(const ipc_session_hello& request, std::string_view expected_token,
                            std::span<const gneiss::protocol::domain_capability> supported_domains,
                            ipc_session_hello& acknowledgment) {
  if (expected_token.empty() || request.token.empty() || request.token.size() > max_token_size ||
      !valid_domains(request.domains) || !valid_domains(supported_domains) ||
      !constant_time_equal(request.token, expected_token)) {
    return gneiss::error::invalid_argument;
  }
  {
    ipc_session_hello accepted;
    accepted.domains.reserve(request.domains.size());
    for (const auto requested : request.domains) {
      const auto supported = std::ranges::find(supported_domains, requested.domain,
                                               &gneiss::protocol::domain_capability::domain);
      if (supported != supported_domains.end()) {
        accepted.domains.push_back({.domain = requested.domain,
                                    .version = (std::min)(requested.version, supported->version)});
      }
    }
    acknowledgment = std::move(accepted);
    return gneiss::error::success;
  }
}

std::span<const gneiss::protocol::operation_descriptor> ipc_session_operations() noexcept {
  return session_operations;
}

ipc_timeout_tracker::ipc_timeout_tracker(clock::duration timeout) noexcept
    : timeout_(timeout > clock::duration::zero() ? timeout : clock::duration::zero()) {}

void ipc_timeout_tracker::reset(clock::time_point now) noexcept { last_observed_ = now; }

bool ipc_timeout_tracker::expired(clock::time_point now) const noexcept {
  return timeout_ == clock::duration::zero() || now - last_observed_ >= timeout_;
}

} // namespace gneiss::host
