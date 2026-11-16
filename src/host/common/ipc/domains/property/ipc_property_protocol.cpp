// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_property_protocol.hpp"
#include <gneiss/engine/error.hpp>
#include "reflected_json.hpp"

#include <yyjson.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

constexpr std::size_t max_json_size = 64U * 1024U;

constexpr std::size_t max_message_size = 1024U;
constexpr std::size_t max_property_string_size = 16U * 1024U;

using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;

bool valid_type_id(gneiss::guid id) noexcept { return !id.is_nil(); }

bool parse_type_id(yyjson_val* value, gneiss::guid& output) {
  if (!yyjson_is_str(value))
    return false;
  const std::string_view text{yyjson_get_str(value), yyjson_get_len(value)};
  auto parsed = gneiss::guid::parse(text);
  if (!parsed || parsed->is_nil() || parsed->string() != text)
    return false;
  output = *parsed;
  return true;
}

bool add_property_value(yyjson_mut_doc* doc, yyjson_mut_val* root,
                        const gneiss::value& value) {
  using namespace gneiss;
  if (const auto* text = std::get_if<std::string>(&value.payload);
      text && text->size() > max_property_string_size)
    return false;
  auto node = gneiss::host::detail::write_reflected_json(doc, value);
  if (!node)
    return false;
  return yyjson_mut_obj_add_val(doc, root, "value", node);
}
bool parse_property_value(yyjson_val* root, gneiss::value& output) {
  using namespace gneiss;
  auto decoded = host::detail::read_reflected_json<gneiss::value>(root);
  if (!decoded)
    return false;
  if (const auto* text = std::get_if<std::string>(&decoded->payload);
      text && text->size() > max_property_string_size)
    return false;
  output = std::move(*decoded);
  return true;
}

bool add_session(yyjson_mut_doc* document, yyjson_mut_val* root,
                 std::uint64_t session_id) noexcept {
  return session_id != 0U && yyjson_mut_obj_add_uint(document, root, "session_id", session_id);
}

bool parse_uint(yyjson_val* root, const char* name, std::uint64_t& output) noexcept {
  auto* value = yyjson_is_obj(root) ? yyjson_obj_get(root, name) : nullptr;
  if (!yyjson_is_uint(value)) {
    return false;
  }
  output = yyjson_get_uint(value);
  return true;
}

gneiss::error write_payload(yyjson_mut_doc* document, yyjson_mut_val* root,
                                    std::vector<std::uint8_t>& output) {
  yyjson_mut_doc_set_root(document, root);
  std::size_t length = 0U;
  std::unique_ptr<char, decltype(&std::free)> json(
      yyjson_mut_write(document, YYJSON_WRITE_NOFLAG, &length), &std::free);
  if (!json || length > max_json_size) {
    return json ? gneiss::error::invalid_argument : gneiss::error::out_of_memory;
  }
  std::vector<std::uint8_t> encoded(reinterpret_cast<const std::uint8_t*>(json.get()),
                                    reinterpret_cast<const std::uint8_t*>(json.get()) + length);
  output = std::move(encoded);
  return gneiss::error::success;
}

} // namespace

namespace gneiss::host {

gneiss::error encode_ipc_property_write(const ipc_property_write& command,
                                                std::vector<std::uint8_t>& output) {
  if (!command.object.is_valid() || !valid_type_id(command.type_id) || command.field_id == 0U ||
      command.expected_revision == 0U) {
    return gneiss::error::invalid_argument;
  }
  {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    const auto type_text = command.type_id.string();
    if (root == nullptr || !add_session(document.get(), root, command.session_id) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "object_id", command.object.value) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "generation", command.object.generation) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "type_id", type_text.data(),
                                    type_text.size()) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "field_id", command.field_id) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "expected_revision",
                                 command.expected_revision) ||
        !add_property_value(document.get(), root, command.value)) {
      return gneiss::error::invalid_argument;
    }
    return write_payload(document.get(), root, output);
  }
}

gneiss::error decode_ipc_property_write(std::span<const std::uint8_t> payload,
                                                ipc_property_write& output) {
  if (payload.size() > max_json_size) {
    return gneiss::error::invalid_argument;
  }
  {
    document_ptr document(yyjson_read(reinterpret_cast<const char*>(payload.data()), payload.size(),
                                      YYJSON_READ_NOFLAG),
                          &yyjson_doc_free);
    if (!document) {
      return gneiss::error::invalid_argument;
    }
    auto* root = yyjson_doc_get_root(document.get());
    ipc_property_write parsed;
    std::uint64_t generation = 0U;
    std::uint64_t field_id = 0U;
    if (!parse_uint(root, "session_id", parsed.session_id) ||
        !parse_uint(root, "object_id", parsed.object.value) ||
        !parse_uint(root, "generation", generation) ||
        generation > std::numeric_limits<std::uint32_t>::max() ||
        !parse_type_id(yyjson_obj_get(root, "type_id"), parsed.type_id) ||
        !parse_uint(root, "field_id", field_id) ||
        field_id > std::numeric_limits<std::uint32_t>::max() ||
        !parse_uint(root, "expected_revision", parsed.expected_revision) ||
        !parse_property_value(yyjson_obj_get(root, "value"), parsed.value)) {
      return gneiss::error::invalid_argument;
    }
    parsed.object.generation = static_cast<std::uint32_t>(generation);
    parsed.field_id = static_cast<std::uint32_t>(field_id);
    if (parsed.session_id == 0U || !parsed.object.is_valid() || parsed.field_id == 0U ||
        parsed.expected_revision == 0U) {
      return gneiss::error::invalid_argument;
    }
    output = std::move(parsed);
    return gneiss::error::success;
  }
}

gneiss::error encode_ipc_property_write_result(const ipc_property_write_result& response,
                                                       std::vector<std::uint8_t>& output) {
  if (response.message.size() > max_message_size ||
      (response.code == 0 && (response.revision == 0U || std::holds_alternative<std::monostate>(
                                                             response.canonical_value.payload)))) {
    return gneiss::error::invalid_argument;
  }
  {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    if (root == nullptr || !add_session(document.get(), root, response.session_id) ||
        !yyjson_mut_obj_add_sint(document.get(), root, "code", response.code) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "revision", response.revision) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "message", response.message.data(),
                                    response.message.size())) {
      return gneiss::error::invalid_argument;
    }
    if (response.code == 0) {
      if (!add_property_value(document.get(), root, response.canonical_value)) {
        return gneiss::error::invalid_argument;
      }
    }
    return write_payload(document.get(), root, output);
  }
}

gneiss::error decode_ipc_property_write_result(std::span<const std::uint8_t> payload,
                                                       ipc_property_write_result& output) {
  if (payload.size() > max_json_size) {
    return gneiss::error::invalid_argument;
  }
  {
    document_ptr document(yyjson_read(reinterpret_cast<const char*>(payload.data()), payload.size(),
                                      YYJSON_READ_NOFLAG),
                          &yyjson_doc_free);
    if (!document) {
      return gneiss::error::invalid_argument;
    }
    auto* root = yyjson_doc_get_root(document.get());
    ipc_property_write_result parsed;
    auto* code = yyjson_is_obj(root) ? yyjson_obj_get(root, "code") : nullptr;
    auto* message = yyjson_is_obj(root) ? yyjson_obj_get(root, "message") : nullptr;
    if (!parse_uint(root, "session_id", parsed.session_id) || !yyjson_is_int(code) ||
        yyjson_get_sint(code) < std::numeric_limits<std::int32_t>::min() ||
        yyjson_get_sint(code) > std::numeric_limits<std::int32_t>::max() ||
        !parse_uint(root, "revision", parsed.revision) || !yyjson_is_str(message) ||
        yyjson_get_len(message) > max_message_size) {
      return gneiss::error::invalid_argument;
    }
    parsed.code = static_cast<std::int32_t>(yyjson_get_sint(code));
    parsed.message.assign(yyjson_get_str(message), yyjson_get_len(message));
    if (parsed.session_id == 0U ||
        (parsed.code == 0 &&
         (parsed.revision == 0U ||
          !parse_property_value(yyjson_obj_get(root, "value"), parsed.canonical_value)))) {
      return gneiss::error::invalid_argument;
    }
    output = std::move(parsed);
    return gneiss::error::success;
  }
}

namespace {

constexpr auto property_request_kind =
    gneiss::ipc_kind_mask(gneiss::ipc_message_kind::request);
constexpr auto property_response_kind =
    gneiss::ipc_kind_mask(gneiss::ipc_message_kind::response);
constexpr std::array property_operations{gneiss::ipc_operation_descriptor{
    .operation = static_cast<std::uint16_t>(ipc_property_operation::write),
    .editor_to_runtime_kinds = property_request_kind,
    .runtime_to_editor_kinds = property_response_kind}};

[[nodiscard]] bool matches_property(const gneiss::ipc_envelope& envelope,
                                    gneiss::ipc_message_kind kind) noexcept {
  return envelope.domain == gneiss::ipc_domain::property &&
         envelope.operation == static_cast<std::uint16_t>(ipc_property_operation::write) &&
         envelope.kind == kind;
}

} // namespace

gneiss::error encode_ipc_property_write_v2(const ipc_property_write& command,
                                                   std::uint32_t request_id,
                                                   gneiss::ipc_envelope& output) {
  if (request_id == 0U || command.command_id != request_id) {
    return gneiss::error::invalid_argument;
  }
  std::vector<std::uint8_t> payload;
  const auto operation = encode_ipc_property_write(command, payload);
  if (operation != gneiss::error::success) {
    return operation;
  }
  output = {.domain = gneiss::ipc_domain::property,
            .operation = static_cast<std::uint16_t>(ipc_property_operation::write),
            .kind = gneiss::ipc_message_kind::request,
            .request_id = request_id,
            .payload = std::move(payload)};
  return gneiss::error::success;
}

gneiss::error decode_ipc_property_write_v2(const gneiss::ipc_envelope& envelope,
                                                   ipc_property_write& output) {
  if (!matches_property(envelope, gneiss::ipc_message_kind::request) ||
      envelope.request_id == 0U) {
    return gneiss::error::invalid_argument;
  }
  ipc_property_write decoded;
  const auto operation = decode_ipc_property_write(envelope.payload, decoded);
  if (operation != gneiss::error::success) {
    return operation;
  }
  decoded.command_id = envelope.request_id;
  output = std::move(decoded);
  return gneiss::error::success;
}

gneiss::error encode_ipc_property_result_v2(const ipc_property_write_result& response,
                                                    std::uint32_t request_id,
                                                    gneiss::ipc_envelope& output) {
  if (request_id == 0U || response.command_id != request_id) {
    return gneiss::error::invalid_argument;
  }
  std::vector<std::uint8_t> payload;
  const auto operation = encode_ipc_property_write_result(response, payload);
  if (operation != gneiss::error::success) {
    return operation;
  }
  output = {.domain = gneiss::ipc_domain::property,
            .operation = static_cast<std::uint16_t>(ipc_property_operation::write),
            .kind = gneiss::ipc_message_kind::response,
            .request_id = request_id,
            .payload = std::move(payload)};
  return gneiss::error::success;
}

gneiss::error decode_ipc_property_result_v2(const gneiss::ipc_envelope& envelope,
                                                    ipc_property_write_result& output) {
  if (!matches_property(envelope, gneiss::ipc_message_kind::response) ||
      envelope.request_id == 0U) {
    return gneiss::error::invalid_argument;
  }
  ipc_property_write_result decoded;
  const auto operation = decode_ipc_property_write_result(envelope.payload, decoded);
  if (operation != gneiss::error::success) {
    return operation;
  }
  decoded.command_id = envelope.request_id;
  output = std::move(decoded);
  return gneiss::error::success;
}

std::span<const gneiss::ipc_operation_descriptor> ipc_property_operations() noexcept {
  return property_operations;
}

} // namespace gneiss::host
