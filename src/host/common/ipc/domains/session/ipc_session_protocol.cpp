// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_session_protocol.hpp"
#include "engine/core/result.hpp"

#include <yyjson.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>

namespace {

constexpr std::size_t max_token_size = 256U;
constexpr std::size_t max_domain_count = 32U;
constexpr std::size_t max_error_message_size = std::size_t{16U} * 1024U;

constexpr std::array session_operations{
    gneiss::engine::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::host::ipc_session_operation::hello),
        .editor_to_runtime_kinds =
            gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::response),
        .runtime_to_editor_kinds =
            gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::request)},
    gneiss::engine::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::host::ipc_session_operation::heartbeat),
        .editor_to_runtime_kinds =
            gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::request),
        .runtime_to_editor_kinds =
            gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::response)},
    gneiss::engine::ipc_operation_descriptor{
        .operation =
            static_cast<std::uint16_t>(gneiss::host::ipc_session_operation::protocol_error),
        .editor_to_runtime_kinds =
            gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::error),
        .runtime_to_editor_kinds =
            gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::error)},
    gneiss::engine::ipc_operation_descriptor{
        .operation =
            static_cast<std::uint16_t>(gneiss::host::ipc_session_operation::shutdown_complete),
        .editor_to_runtime_kinds = 0U,
        .runtime_to_editor_kinds =
            gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::event)}};

using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;

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
valid_domains(std::span<const gneiss::engine::ipc_domain_capability> domains) noexcept {
  if (domains.size() > max_domain_count) {
    return false;
  }
  for (std::size_t index = 0U; index < domains.size(); ++index) {
    const auto remaining = domains.subspan(index + 1U);
    if (static_cast<std::uint16_t>(domains[index].domain) == 0U || domains[index].version == 0U ||
        std::ranges::find(remaining, domains[index].domain,
                          &gneiss::engine::ipc_domain_capability::domain) != remaining.end()) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] gneiss::engine::result write_document(yyjson_mut_doc* document, yyjson_mut_val* root,
                                                    gneiss::engine::ipc_envelope& envelope) {
  yyjson_mut_doc_set_root(document, root);
  std::size_t length = 0U;
  std::unique_ptr<char, decltype(&std::free)> json(
      yyjson_mut_write(document, YYJSON_WRITE_NOFLAG, &length), &std::free);
  if (!json) {
    return gneiss::engine::result::out_of_memory;
  }
  if (length > gneiss::host::ipc_session_max_payload_size) {
    return gneiss::engine::result::invalid_argument;
  }
  envelope.payload.assign(reinterpret_cast<const std::uint8_t*>(json.get()),
                          reinterpret_cast<const std::uint8_t*>(json.get()) + length);
  return gneiss::engine::result::success;
}

[[nodiscard]] document_ptr read_document(const gneiss::engine::ipc_envelope& envelope) noexcept {
  return {yyjson_read(reinterpret_cast<const char*>(envelope.payload.data()),
                      envelope.payload.size(), YYJSON_READ_NOFLAG),
          &yyjson_doc_free};
}

[[nodiscard]] bool matches(const gneiss::engine::ipc_envelope& envelope,
                           gneiss::host::ipc_session_operation operation) noexcept {
  return envelope.domain == gneiss::engine::ipc_domain::session &&
         envelope.operation == static_cast<std::uint16_t>(operation) &&
         envelope.protocol_major == gneiss::engine::ipc_v2_protocol_major &&
         envelope.protocol_minor <= gneiss::engine::ipc_v2_protocol_minor;
}

} // namespace

namespace gneiss::host {

gneiss::engine::result encode_ipc_session_hello(const ipc_session_hello& message, bool response,
                                                std::uint32_t request_id,
                                                gneiss::engine::ipc_envelope& output) noexcept {
  if ((!response && (message.token.empty() || message.token.size() > max_token_size)) ||
      (response && !message.token.empty()) || request_id == 0U || !valid_domains(message.domains)) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    auto* domains = document ? yyjson_mut_arr(document.get()) : nullptr;
    if (!document || root == nullptr || domains == nullptr) {
      return gneiss::engine::result::out_of_memory;
    }
    if (!response && !yyjson_mut_obj_add_strncpy(document.get(), root, "token",
                                                 message.token.data(), message.token.size())) {
      return gneiss::engine::result::out_of_memory;
    }
    for (const auto capability : message.domains) {
      auto* item = yyjson_mut_obj(document.get());
      if (item == nullptr ||
          !yyjson_mut_obj_add_uint(document.get(), item, "domain",
                                   static_cast<std::uint16_t>(capability.domain)) ||
          !yyjson_mut_obj_add_uint(document.get(), item, "version", capability.version) ||
          !yyjson_mut_arr_add_val(domains, item)) {
        return gneiss::engine::result::out_of_memory;
      }
    }
    if (!yyjson_mut_obj_add_val(document.get(), root, "domains", domains)) {
      return gneiss::engine::result::out_of_memory;
    }
    gneiss::engine::ipc_envelope encoded{
        .domain = gneiss::engine::ipc_domain::session,
        .operation = static_cast<std::uint16_t>(ipc_session_operation::hello),
        .kind = response ? gneiss::engine::ipc_message_kind::response
                         : gneiss::engine::ipc_message_kind::request,
        .request_id = request_id,
        .payload = {}};
    const auto operation = write_document(document.get(), root, encoded);
    if (operation == gneiss::engine::result::success) {
      output = std::move(encoded);
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result decode_ipc_session_hello(const gneiss::engine::ipc_envelope& envelope,
                                                ipc_session_hello& output) noexcept {
  if (!matches(envelope, ipc_session_operation::hello) ||
      (envelope.kind != gneiss::engine::ipc_message_kind::request &&
       envelope.kind != gneiss::engine::ipc_message_kind::response) ||
      envelope.request_id == 0U || envelope.payload.size() > ipc_session_max_payload_size) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    auto document = read_document(envelope);
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    auto* token = yyjson_is_obj(root) ? yyjson_obj_get(root, "token") : nullptr;
    auto* domains = yyjson_is_obj(root) ? yyjson_obj_get(root, "domains") : nullptr;
    if (!yyjson_is_arr(domains) || yyjson_arr_size(domains) > max_domain_count ||
        (envelope.kind == gneiss::engine::ipc_message_kind::request &&
         (!yyjson_is_str(token) || yyjson_get_len(token) == 0U ||
          yyjson_get_len(token) > max_token_size)) ||
        (envelope.kind == gneiss::engine::ipc_message_kind::response && token != nullptr)) {
      return gneiss::engine::result::invalid_argument;
    }
    ipc_session_hello decoded;
    if (envelope.kind == gneiss::engine::ipc_message_kind::request) {
      decoded.token.assign(yyjson_get_str(token), yyjson_get_len(token));
    }
    decoded.domains.reserve(yyjson_arr_size(domains));
    std::size_t index = 0U;
    std::size_t count = 0U;
    yyjson_val* item = nullptr;
    yyjson_arr_foreach(domains, index, count, item) {
      auto* domain = yyjson_is_obj(item) ? yyjson_obj_get(item, "domain") : nullptr;
      auto* version = yyjson_is_obj(item) ? yyjson_obj_get(item, "version") : nullptr;
      if (!yyjson_is_uint(domain) || yyjson_get_uint(domain) == 0U ||
          yyjson_get_uint(domain) > std::numeric_limits<std::uint16_t>::max() ||
          !yyjson_is_uint(version) || yyjson_get_uint(version) == 0U ||
          yyjson_get_uint(version) > std::numeric_limits<std::uint16_t>::max()) {
        return gneiss::engine::result::invalid_argument;
      }
      decoded.domains.push_back(
          {.domain = static_cast<gneiss::engine::ipc_domain>(yyjson_get_uint(domain)),
           .version = static_cast<std::uint16_t>(yyjson_get_uint(version))});
    }
    if (!valid_domains(decoded.domains)) {
      return gneiss::engine::result::invalid_argument;
    }
    output = std::move(decoded);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result encode_ipc_session_heartbeat(const ipc_session_heartbeat& message,
                                                    bool response, std::uint32_t request_id,
                                                    gneiss::engine::ipc_envelope& output) noexcept {
  if (request_id == 0U) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    if (!document || root == nullptr ||
        !yyjson_mut_obj_add_uint(document.get(), root, "nonce", message.nonce)) {
      return gneiss::engine::result::out_of_memory;
    }
    gneiss::engine::ipc_envelope encoded{
        .domain = gneiss::engine::ipc_domain::session,
        .operation = static_cast<std::uint16_t>(ipc_session_operation::heartbeat),
        .kind = response ? gneiss::engine::ipc_message_kind::response
                         : gneiss::engine::ipc_message_kind::request,
        .request_id = request_id,
        .payload = {}};
    const auto operation = write_document(document.get(), root, encoded);
    if (operation == gneiss::engine::result::success) {
      output = std::move(encoded);
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result decode_ipc_session_heartbeat(const gneiss::engine::ipc_envelope& envelope,
                                                    ipc_session_heartbeat& output) noexcept {
  if (!matches(envelope, ipc_session_operation::heartbeat) ||
      (envelope.kind != gneiss::engine::ipc_message_kind::request &&
       envelope.kind != gneiss::engine::ipc_message_kind::response) ||
      envelope.request_id == 0U || envelope.payload.size() > ipc_session_max_payload_size) {
    return gneiss::engine::result::invalid_argument;
  }
  auto document = read_document(envelope);
  auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
  auto* nonce = yyjson_is_obj(root) ? yyjson_obj_get(root, "nonce") : nullptr;
  if (!yyjson_is_uint(nonce)) {
    return gneiss::engine::result::invalid_argument;
  }
  output.nonce = yyjson_get_uint(nonce);
  return gneiss::engine::result::success;
}

gneiss::engine::result encode_ipc_session_error(const ipc_session_error& message,
                                                std::uint32_t request_id,
                                                gneiss::engine::ipc_envelope& output) noexcept {
  if (request_id == 0U || message.message.size() > max_error_message_size) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    if (!document || root == nullptr ||
        !yyjson_mut_obj_add_sint(document.get(), root, "code", message.code) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "message", message.message.data(),
                                    message.message.size())) {
      return gneiss::engine::result::out_of_memory;
    }
    gneiss::engine::ipc_envelope encoded{
        .domain = gneiss::engine::ipc_domain::session,
        .operation = static_cast<std::uint16_t>(ipc_session_operation::protocol_error),
        .kind = gneiss::engine::ipc_message_kind::error,
        .request_id = request_id,
        .payload = {}};
    const auto operation = write_document(document.get(), root, encoded);
    if (operation == gneiss::engine::result::success) {
      output = std::move(encoded);
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result decode_ipc_session_error(const gneiss::engine::ipc_envelope& envelope,
                                                ipc_session_error& output) noexcept {
  if (!matches(envelope, ipc_session_operation::protocol_error) ||
      envelope.kind != gneiss::engine::ipc_message_kind::error || envelope.request_id == 0U ||
      envelope.payload.size() > ipc_session_max_payload_size) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    auto document = read_document(envelope);
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    auto* code = yyjson_is_obj(root) ? yyjson_obj_get(root, "code") : nullptr;
    auto* message = yyjson_is_obj(root) ? yyjson_obj_get(root, "message") : nullptr;
    if (!yyjson_is_int(code) || yyjson_get_sint(code) < std::numeric_limits<std::int32_t>::min() ||
        yyjson_get_sint(code) > std::numeric_limits<std::int32_t>::max() ||
        !yyjson_is_str(message) || yyjson_get_len(message) > max_error_message_size) {
      return gneiss::engine::result::invalid_argument;
    }
    ipc_session_error decoded{.code = static_cast<std::int32_t>(yyjson_get_sint(code)),
                              .message = {yyjson_get_str(message), yyjson_get_len(message)}};
    output = std::move(decoded);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result encode_ipc_session_shutdown(const ipc_session_shutdown& message,
                                                   gneiss::engine::ipc_envelope& output) noexcept {
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    if (!document || root == nullptr ||
        !yyjson_mut_obj_add_sint(document.get(), root, "exit_code", message.exit_code)) {
      return gneiss::engine::result::out_of_memory;
    }
    gneiss::engine::ipc_envelope encoded{
        .domain = gneiss::engine::ipc_domain::session,
        .operation = static_cast<std::uint16_t>(ipc_session_operation::shutdown_complete),
        .kind = gneiss::engine::ipc_message_kind::event,
        .request_id = 0U,
        .payload = {}};
    const auto operation = write_document(document.get(), root, encoded);
    if (operation == gneiss::engine::result::success) {
      output = std::move(encoded);
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result decode_ipc_session_shutdown(const gneiss::engine::ipc_envelope& envelope,
                                                   ipc_session_shutdown& output) noexcept {
  if (!matches(envelope, ipc_session_operation::shutdown_complete) ||
      envelope.kind != gneiss::engine::ipc_message_kind::event || envelope.request_id != 0U ||
      envelope.payload.size() > ipc_session_max_payload_size) {
    return gneiss::engine::result::invalid_argument;
  }
  auto document = read_document(envelope);
  auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
  auto* exit_code = yyjson_is_obj(root) ? yyjson_obj_get(root, "exit_code") : nullptr;
  if (!yyjson_is_int(exit_code) ||
      yyjson_get_sint(exit_code) < std::numeric_limits<std::int32_t>::min() ||
      yyjson_get_sint(exit_code) > std::numeric_limits<std::int32_t>::max()) {
    return gneiss::engine::result::invalid_argument;
  }
  output.exit_code = static_cast<std::int32_t>(yyjson_get_sint(exit_code));
  return gneiss::engine::result::success;
}

gneiss::engine::result negotiate_ipc_session_hello(
    const ipc_session_hello& request, std::string_view expected_token,
    std::span<const gneiss::engine::ipc_domain_capability> supported_domains,
    ipc_session_hello& acknowledgment) noexcept {
  if (expected_token.empty() || request.token.empty() || request.token.size() > max_token_size ||
      !valid_domains(request.domains) || !valid_domains(supported_domains) ||
      !constant_time_equal(request.token, expected_token)) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    ipc_session_hello accepted;
    accepted.domains.reserve(request.domains.size());
    for (const auto requested : request.domains) {
      const auto supported = std::ranges::find(supported_domains, requested.domain,
                                               &gneiss::engine::ipc_domain_capability::domain);
      if (supported != supported_domains.end()) {
        accepted.domains.push_back({.domain = requested.domain,
                                    .version = (std::min)(requested.version, supported->version)});
      }
    }
    acknowledgment = std::move(accepted);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

std::span<const gneiss::engine::ipc_operation_descriptor> ipc_session_operations() noexcept {
  return session_operations;
}

ipc_timeout_tracker::ipc_timeout_tracker(clock::duration timeout) noexcept
    : timeout_(timeout > clock::duration::zero() ? timeout : clock::duration::zero()) {}

void ipc_timeout_tracker::reset(clock::time_point now) noexcept { last_observed_ = now; }

bool ipc_timeout_tracker::expired(clock::time_point now) const noexcept {
  return timeout_ == clock::duration::zero() || now - last_observed_ >= timeout_;
}

} // namespace gneiss::host
