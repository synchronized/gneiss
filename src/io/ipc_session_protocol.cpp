// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_session_protocol.h"

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
    gneiss::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::ipc_session_operation::hello),
        .editor_to_runtime_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::response),
        .runtime_to_editor_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::request)},
    gneiss::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::ipc_session_operation::heartbeat),
        .editor_to_runtime_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::request),
        .runtime_to_editor_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::response)},
    gneiss::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::ipc_session_operation::protocol_error),
        .editor_to_runtime_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::error),
        .runtime_to_editor_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::error)},
    gneiss::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::ipc_session_operation::shutdown_complete),
        .editor_to_runtime_kinds = 0U,
        .runtime_to_editor_kinds = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::event)}};

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

[[nodiscard]] bool valid_domains(std::span<const gneiss::ipc_domain_capability> domains) noexcept {
  if (domains.size() > max_domain_count) {
    return false;
  }
  for (std::size_t index = 0U; index < domains.size(); ++index) {
    const auto remaining = domains.subspan(index + 1U);
    if (static_cast<std::uint16_t>(domains[index].domain) == 0U || domains[index].version == 0U ||
        std::ranges::find(remaining, domains[index].domain,
                          &gneiss::ipc_domain_capability::domain) != remaining.end()) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] gneiss::result write_document(yyjson_mut_doc* document, yyjson_mut_val* root,
                                            gneiss::ipc_envelope& envelope) {
  yyjson_mut_doc_set_root(document, root);
  std::size_t length = 0U;
  std::unique_ptr<char, decltype(&std::free)> json(
      yyjson_mut_write(document, YYJSON_WRITE_NOFLAG, &length), &std::free);
  if (!json) {
    return gneiss::result::out_of_memory;
  }
  if (length > gneiss::ipc_session_max_payload_size) {
    return gneiss::result::invalid_argument;
  }
  envelope.payload.assign(reinterpret_cast<const std::uint8_t*>(json.get()),
                          reinterpret_cast<const std::uint8_t*>(json.get()) + length);
  return gneiss::result::success;
}

[[nodiscard]] document_ptr read_document(const gneiss::ipc_envelope& envelope) noexcept {
  return {yyjson_read(reinterpret_cast<const char*>(envelope.payload.data()),
                      envelope.payload.size(), YYJSON_READ_NOFLAG),
          &yyjson_doc_free};
}

[[nodiscard]] bool matches(const gneiss::ipc_envelope& envelope,
                           gneiss::ipc_session_operation operation) noexcept {
  return envelope.domain == gneiss::ipc_domain::session &&
         envelope.operation == static_cast<std::uint16_t>(operation) &&
         envelope.protocol_major == gneiss::ipc_v2_protocol_major &&
         envelope.protocol_minor <= gneiss::ipc_v2_protocol_minor;
}

} // namespace

namespace gneiss {

result encode_ipc_session_hello(const ipc_session_hello& message, bool response,
                                std::uint32_t request_id, ipc_envelope& output) noexcept {
  if ((!response && (message.token.empty() || message.token.size() > max_token_size)) ||
      (response && !message.token.empty()) || request_id == 0U || !valid_domains(message.domains)) {
    return result::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    auto* domains = document ? yyjson_mut_arr(document.get()) : nullptr;
    if (!document || root == nullptr || domains == nullptr) {
      return result::out_of_memory;
    }
    if (!response && !yyjson_mut_obj_add_strncpy(document.get(), root, "token",
                                                 message.token.data(), message.token.size())) {
      return result::out_of_memory;
    }
    for (const auto capability : message.domains) {
      auto* item = yyjson_mut_obj(document.get());
      if (item == nullptr ||
          !yyjson_mut_obj_add_uint(document.get(), item, "domain",
                                   static_cast<std::uint16_t>(capability.domain)) ||
          !yyjson_mut_obj_add_uint(document.get(), item, "version", capability.version) ||
          !yyjson_mut_arr_add_val(domains, item)) {
        return result::out_of_memory;
      }
    }
    if (!yyjson_mut_obj_add_val(document.get(), root, "domains", domains)) {
      return result::out_of_memory;
    }
    ipc_envelope encoded{.domain = ipc_domain::session,
                         .operation = static_cast<std::uint16_t>(ipc_session_operation::hello),
                         .kind = response ? ipc_message_kind::response : ipc_message_kind::request,
                         .request_id = request_id,
                         .payload = {}};
    const auto operation = write_document(document.get(), root, encoded);
    if (operation == result::success) {
      output = std::move(encoded);
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_session_hello(const ipc_envelope& envelope, ipc_session_hello& output) noexcept {
  if (!matches(envelope, ipc_session_operation::hello) ||
      (envelope.kind != ipc_message_kind::request && envelope.kind != ipc_message_kind::response) ||
      envelope.request_id == 0U || envelope.payload.size() > ipc_session_max_payload_size) {
    return result::invalid_argument;
  }
  try {
    auto document = read_document(envelope);
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    auto* token = yyjson_is_obj(root) ? yyjson_obj_get(root, "token") : nullptr;
    auto* domains = yyjson_is_obj(root) ? yyjson_obj_get(root, "domains") : nullptr;
    if (!yyjson_is_arr(domains) || yyjson_arr_size(domains) > max_domain_count ||
        (envelope.kind == ipc_message_kind::request &&
         (!yyjson_is_str(token) || yyjson_get_len(token) == 0U ||
          yyjson_get_len(token) > max_token_size)) ||
        (envelope.kind == ipc_message_kind::response && token != nullptr)) {
      return result::invalid_argument;
    }
    ipc_session_hello decoded;
    if (envelope.kind == ipc_message_kind::request) {
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
        return result::invalid_argument;
      }
      decoded.domains.push_back({.domain = static_cast<ipc_domain>(yyjson_get_uint(domain)),
                                 .version = static_cast<std::uint16_t>(yyjson_get_uint(version))});
    }
    if (!valid_domains(decoded.domains)) {
      return result::invalid_argument;
    }
    output = std::move(decoded);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result encode_ipc_session_heartbeat(const ipc_session_heartbeat& message, bool response,
                                    std::uint32_t request_id, ipc_envelope& output) noexcept {
  if (request_id == 0U) {
    return result::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    if (!document || root == nullptr ||
        !yyjson_mut_obj_add_uint(document.get(), root, "nonce", message.nonce)) {
      return result::out_of_memory;
    }
    ipc_envelope encoded{.domain = ipc_domain::session,
                         .operation = static_cast<std::uint16_t>(ipc_session_operation::heartbeat),
                         .kind = response ? ipc_message_kind::response : ipc_message_kind::request,
                         .request_id = request_id,
                         .payload = {}};
    const auto operation = write_document(document.get(), root, encoded);
    if (operation == result::success) {
      output = std::move(encoded);
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_session_heartbeat(const ipc_envelope& envelope,
                                    ipc_session_heartbeat& output) noexcept {
  if (!matches(envelope, ipc_session_operation::heartbeat) ||
      (envelope.kind != ipc_message_kind::request && envelope.kind != ipc_message_kind::response) ||
      envelope.request_id == 0U || envelope.payload.size() > ipc_session_max_payload_size) {
    return result::invalid_argument;
  }
  auto document = read_document(envelope);
  auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
  auto* nonce = yyjson_is_obj(root) ? yyjson_obj_get(root, "nonce") : nullptr;
  if (!yyjson_is_uint(nonce)) {
    return result::invalid_argument;
  }
  output.nonce = yyjson_get_uint(nonce);
  return result::success;
}

result encode_ipc_session_error(const ipc_session_error& message, std::uint32_t request_id,
                                ipc_envelope& output) noexcept {
  if (request_id == 0U || message.message.size() > max_error_message_size) {
    return result::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    if (!document || root == nullptr ||
        !yyjson_mut_obj_add_sint(document.get(), root, "code", message.code) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "message", message.message.data(),
                                    message.message.size())) {
      return result::out_of_memory;
    }
    ipc_envelope encoded{.domain = ipc_domain::session,
                         .operation =
                             static_cast<std::uint16_t>(ipc_session_operation::protocol_error),
                         .kind = ipc_message_kind::error,
                         .request_id = request_id,
                         .payload = {}};
    const auto operation = write_document(document.get(), root, encoded);
    if (operation == result::success) {
      output = std::move(encoded);
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_session_error(const ipc_envelope& envelope, ipc_session_error& output) noexcept {
  if (!matches(envelope, ipc_session_operation::protocol_error) ||
      envelope.kind != ipc_message_kind::error || envelope.request_id == 0U ||
      envelope.payload.size() > ipc_session_max_payload_size) {
    return result::invalid_argument;
  }
  try {
    auto document = read_document(envelope);
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    auto* code = yyjson_is_obj(root) ? yyjson_obj_get(root, "code") : nullptr;
    auto* message = yyjson_is_obj(root) ? yyjson_obj_get(root, "message") : nullptr;
    if (!yyjson_is_int(code) || yyjson_get_sint(code) < std::numeric_limits<std::int32_t>::min() ||
        yyjson_get_sint(code) > std::numeric_limits<std::int32_t>::max() ||
        !yyjson_is_str(message) || yyjson_get_len(message) > max_error_message_size) {
      return result::invalid_argument;
    }
    ipc_session_error decoded{.code = static_cast<std::int32_t>(yyjson_get_sint(code)),
                              .message = {yyjson_get_str(message), yyjson_get_len(message)}};
    output = std::move(decoded);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result encode_ipc_session_shutdown(const ipc_session_shutdown& message,
                                   ipc_envelope& output) noexcept {
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    if (!document || root == nullptr ||
        !yyjson_mut_obj_add_sint(document.get(), root, "exit_code", message.exit_code)) {
      return result::out_of_memory;
    }
    ipc_envelope encoded{.domain = ipc_domain::session,
                         .operation =
                             static_cast<std::uint16_t>(ipc_session_operation::shutdown_complete),
                         .kind = ipc_message_kind::event,
                         .request_id = 0U,
                         .payload = {}};
    const auto operation = write_document(document.get(), root, encoded);
    if (operation == result::success) {
      output = std::move(encoded);
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_session_shutdown(const ipc_envelope& envelope,
                                   ipc_session_shutdown& output) noexcept {
  if (!matches(envelope, ipc_session_operation::shutdown_complete) ||
      envelope.kind != ipc_message_kind::event || envelope.request_id != 0U ||
      envelope.payload.size() > ipc_session_max_payload_size) {
    return result::invalid_argument;
  }
  auto document = read_document(envelope);
  auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
  auto* exit_code = yyjson_is_obj(root) ? yyjson_obj_get(root, "exit_code") : nullptr;
  if (!yyjson_is_int(exit_code) ||
      yyjson_get_sint(exit_code) < std::numeric_limits<std::int32_t>::min() ||
      yyjson_get_sint(exit_code) > std::numeric_limits<std::int32_t>::max()) {
    return result::invalid_argument;
  }
  output.exit_code = static_cast<std::int32_t>(yyjson_get_sint(exit_code));
  return result::success;
}

result negotiate_ipc_session_hello(const ipc_session_hello& request,
                                   std::string_view expected_token,
                                   std::span<const ipc_domain_capability> supported_domains,
                                   ipc_session_hello& acknowledgment) noexcept {
  if (expected_token.empty() || request.token.empty() || request.token.size() > max_token_size ||
      !valid_domains(request.domains) || !valid_domains(supported_domains) ||
      !constant_time_equal(request.token, expected_token)) {
    return result::invalid_argument;
  }
  try {
    ipc_session_hello accepted;
    accepted.domains.reserve(request.domains.size());
    for (const auto requested : request.domains) {
      const auto supported =
          std::ranges::find(supported_domains, requested.domain, &ipc_domain_capability::domain);
      if (supported != supported_domains.end()) {
        accepted.domains.push_back({.domain = requested.domain,
                                    .version = (std::min)(requested.version, supported->version)});
      }
    }
    acknowledgment = std::move(accepted);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

std::span<const ipc_operation_descriptor> ipc_session_operations() noexcept {
  return session_operations;
}

} // namespace gneiss
