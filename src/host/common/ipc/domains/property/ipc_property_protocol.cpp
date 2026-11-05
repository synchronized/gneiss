// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_property_protocol.hpp"
#include "engine/core/error.hpp"
#include "engine/core/serialization/detail/json_adapter.hpp"

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
constexpr char hex_digits[] = "0123456789abcdef";

using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;

bool valid_type_id(gneiss::engine::guid id) noexcept { return !id.is_nil(); }

std::string type_id_text(gneiss::engine::guid id) {
  std::string text(32U, '0');
  for (std::size_t index = 0U; index < 16U; ++index) {
    text[index * 2U] = hex_digits[id.bytes()[index] >> 4U];
    text[index * 2U + 1U] = hex_digits[id.bytes()[index] & 0x0FU];
  }
  return text;
}

int hex_value(char value) noexcept {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

bool parse_type_id(yyjson_val* value, gneiss::engine::guid& output) noexcept {
  if (!yyjson_is_str(value) || yyjson_get_len(value) != 32U) {
    return false;
  }
  gneiss::engine::guid::byte_array bytes{};
  const auto* text = yyjson_get_str(value);
  for (std::size_t index = 0U; index < 16U; ++index) {
    const auto high = hex_value(text[index * 2U]);
    const auto low = hex_value(text[index * 2U + 1U]);
    if (high < 0 || low < 0) {
      return false;
    }
    bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  const gneiss::engine::guid parsed{gneiss::engine::guid::from_bytes(bytes)};
  if (!valid_type_id(parsed)) {
    return false;
  }
  output = parsed;
  return true;
}

bool add_property_value(yyjson_mut_doc* doc, yyjson_mut_val* root,
                        const gneiss::engine::value& value) {
  using namespace gneiss::engine;
  if (const auto* text = std::get_if<std::string>(&value.payload);
      text && text->size() > max_property_string_size)
    return false;
  // 旧 IPC GUID 使用 type_id 标签和 32 位十六进制文本；只保留这层协议适配。
  if (const auto* id = std::get_if<guid>(&value.payload)) {
    if (!valid_type_id(*id))
      return false;
    const auto text = type_id_text(*id);
    return yyjson_mut_obj_add_str(doc, root, "kind", "type_id") &&
           yyjson_mut_obj_add_strncpy(doc, root, "value", text.data(), text.size());
  }
  auto node = gneiss::engine::serialization::json::write_value(doc, value);
  if (!node)
    return false;
  return yyjson_mut_obj_add_str(doc, root, "kind",
                                yyjson_mut_get_str(yyjson_mut_obj_get(*node, "kind"))) &&
         yyjson_mut_obj_add_val(doc, root, "value", yyjson_mut_obj_remove_key(*node, "value"));
}
bool parse_property_value(yyjson_val* root, gneiss::engine::value& output) {
  using namespace gneiss::engine;
  auto* tag = yyjson_obj_get(root, "kind");
  auto* content = yyjson_obj_get(root, "value");
  if (!yyjson_is_obj(root) || yyjson_obj_size(root) != 2 || !yyjson_is_str(tag))
    return false;
  const std::string_view kind{yyjson_get_str(tag), yyjson_get_len(tag)};
  if (kind == "type_id") {
    guid id;
    if (!parse_type_id(content, id))
      return false;
    output = value{id};
    return true;
  }
  if (kind == "guid" || (kind == "string" && yyjson_is_str(content) &&
                         yyjson_get_len(content) > max_property_string_size))
    return false;
  auto decoded = gneiss::engine::serialization::json::read_value(root);
  if (!decoded)
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

gneiss::engine::error write_payload(yyjson_mut_doc* document, yyjson_mut_val* root,
                                    std::vector<std::uint8_t>& output) {
  yyjson_mut_doc_set_root(document, root);
  std::size_t length = 0U;
  std::unique_ptr<char, decltype(&std::free)> json(
      yyjson_mut_write(document, YYJSON_WRITE_NOFLAG, &length), &std::free);
  if (!json || length > max_json_size) {
    return json ? gneiss::engine::error::invalid_argument : gneiss::engine::error::out_of_memory;
  }
  std::vector<std::uint8_t> encoded(reinterpret_cast<const std::uint8_t*>(json.get()),
                                    reinterpret_cast<const std::uint8_t*>(json.get()) + length);
  output = std::move(encoded);
  return gneiss::engine::error::success;
}

} // namespace

namespace gneiss::host {

gneiss::engine::error encode_ipc_property_write(const ipc_property_write& command,
                                                std::vector<std::uint8_t>& output) noexcept {
  if (!command.object.is_valid() || !valid_type_id(command.type_id) || command.field_id == 0U ||
      command.expected_revision == 0U) {
    return gneiss::engine::error::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    auto* value = document ? yyjson_mut_obj(document.get()) : nullptr;
    const auto type_text = type_id_text(command.type_id);
    if (root == nullptr || value == nullptr ||
        !add_session(document.get(), root, command.session_id) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "object_id", command.object.value) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "generation", command.object.generation) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "type_id", type_text.data(),
                                    type_text.size()) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "field_id", command.field_id) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "expected_revision",
                                 command.expected_revision) ||
        !add_property_value(document.get(), value, command.value) ||
        !yyjson_mut_obj_add_val(document.get(), root, "value", value)) {
      return gneiss::engine::error::invalid_argument;
    }
    return write_payload(document.get(), root, output);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

gneiss::engine::error decode_ipc_property_write(std::span<const std::uint8_t> payload,
                                                ipc_property_write& output) noexcept {
  if (payload.size() > max_json_size) {
    return gneiss::engine::error::invalid_argument;
  }
  try {
    document_ptr document(yyjson_read(reinterpret_cast<const char*>(payload.data()), payload.size(),
                                      YYJSON_READ_NOFLAG),
                          &yyjson_doc_free);
    if (!document) {
      return gneiss::engine::error::invalid_argument;
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
      return gneiss::engine::error::invalid_argument;
    }
    parsed.object.generation = static_cast<std::uint32_t>(generation);
    parsed.field_id = static_cast<std::uint32_t>(field_id);
    if (parsed.session_id == 0U || !parsed.object.is_valid() || parsed.field_id == 0U ||
        parsed.expected_revision == 0U) {
      return gneiss::engine::error::invalid_argument;
    }
    output = std::move(parsed);
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

gneiss::engine::error encode_ipc_property_write_result(const ipc_property_write_result& response,
                                                       std::vector<std::uint8_t>& output) noexcept {
  if (response.message.size() > max_message_size ||
      (response.code == 0 && (response.revision == 0U || std::holds_alternative<std::monostate>(
                                                             response.canonical_value.payload)))) {
    return gneiss::engine::error::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    if (root == nullptr || !add_session(document.get(), root, response.session_id) ||
        !yyjson_mut_obj_add_sint(document.get(), root, "code", response.code) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "revision", response.revision) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "message", response.message.data(),
                                    response.message.size())) {
      return gneiss::engine::error::invalid_argument;
    }
    if (response.code == 0) {
      auto* value = yyjson_mut_obj(document.get());
      if (value == nullptr ||
          !add_property_value(document.get(), value, response.canonical_value) ||
          !yyjson_mut_obj_add_val(document.get(), root, "value", value)) {
        return gneiss::engine::error::invalid_argument;
      }
    }
    return write_payload(document.get(), root, output);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

gneiss::engine::error decode_ipc_property_write_result(std::span<const std::uint8_t> payload,
                                                       ipc_property_write_result& output) noexcept {
  if (payload.size() > max_json_size) {
    return gneiss::engine::error::invalid_argument;
  }
  try {
    document_ptr document(yyjson_read(reinterpret_cast<const char*>(payload.data()), payload.size(),
                                      YYJSON_READ_NOFLAG),
                          &yyjson_doc_free);
    if (!document) {
      return gneiss::engine::error::invalid_argument;
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
      return gneiss::engine::error::invalid_argument;
    }
    parsed.code = static_cast<std::int32_t>(yyjson_get_sint(code));
    parsed.message.assign(yyjson_get_str(message), yyjson_get_len(message));
    if (parsed.session_id == 0U ||
        (parsed.code == 0 &&
         (parsed.revision == 0U ||
          !parse_property_value(yyjson_obj_get(root, "value"), parsed.canonical_value)))) {
      return gneiss::engine::error::invalid_argument;
    }
    output = std::move(parsed);
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

namespace {

constexpr auto property_request_kind =
    gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::request);
constexpr auto property_response_kind =
    gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::response);
constexpr std::array property_operations{gneiss::engine::ipc_operation_descriptor{
    .operation = static_cast<std::uint16_t>(ipc_property_operation::write),
    .editor_to_runtime_kinds = property_request_kind,
    .runtime_to_editor_kinds = property_response_kind}};

[[nodiscard]] bool matches_property(const gneiss::engine::ipc_envelope& envelope,
                                    gneiss::engine::ipc_message_kind kind) noexcept {
  return envelope.domain == gneiss::engine::ipc_domain::property &&
         envelope.operation == static_cast<std::uint16_t>(ipc_property_operation::write) &&
         envelope.kind == kind;
}

} // namespace

gneiss::engine::error encode_ipc_property_write_v2(const ipc_property_write& command,
                                                   std::uint32_t request_id,
                                                   gneiss::engine::ipc_envelope& output) noexcept {
  if (request_id == 0U || command.command_id != request_id) {
    return gneiss::engine::error::invalid_argument;
  }
  std::vector<std::uint8_t> payload;
  const auto operation = encode_ipc_property_write(command, payload);
  if (operation != gneiss::engine::error::success) {
    return operation;
  }
  output = {.domain = gneiss::engine::ipc_domain::property,
            .operation = static_cast<std::uint16_t>(ipc_property_operation::write),
            .kind = gneiss::engine::ipc_message_kind::request,
            .request_id = request_id,
            .payload = std::move(payload)};
  return gneiss::engine::error::success;
}

gneiss::engine::error decode_ipc_property_write_v2(const gneiss::engine::ipc_envelope& envelope,
                                                   ipc_property_write& output) noexcept {
  if (!matches_property(envelope, gneiss::engine::ipc_message_kind::request) ||
      envelope.request_id == 0U) {
    return gneiss::engine::error::invalid_argument;
  }
  ipc_property_write decoded;
  const auto operation = decode_ipc_property_write(envelope.payload, decoded);
  if (operation != gneiss::engine::error::success) {
    return operation;
  }
  decoded.command_id = envelope.request_id;
  output = std::move(decoded);
  return gneiss::engine::error::success;
}

gneiss::engine::error encode_ipc_property_result_v2(const ipc_property_write_result& response,
                                                    std::uint32_t request_id,
                                                    gneiss::engine::ipc_envelope& output) noexcept {
  if (request_id == 0U || response.command_id != request_id) {
    return gneiss::engine::error::invalid_argument;
  }
  std::vector<std::uint8_t> payload;
  const auto operation = encode_ipc_property_write_result(response, payload);
  if (operation != gneiss::engine::error::success) {
    return operation;
  }
  output = {.domain = gneiss::engine::ipc_domain::property,
            .operation = static_cast<std::uint16_t>(ipc_property_operation::write),
            .kind = gneiss::engine::ipc_message_kind::response,
            .request_id = request_id,
            .payload = std::move(payload)};
  return gneiss::engine::error::success;
}

gneiss::engine::error decode_ipc_property_result_v2(const gneiss::engine::ipc_envelope& envelope,
                                                    ipc_property_write_result& output) noexcept {
  if (!matches_property(envelope, gneiss::engine::ipc_message_kind::response) ||
      envelope.request_id == 0U) {
    return gneiss::engine::error::invalid_argument;
  }
  ipc_property_write_result decoded;
  const auto operation = decode_ipc_property_write_result(envelope.payload, decoded);
  if (operation != gneiss::engine::error::success) {
    return operation;
  }
  decoded.command_id = envelope.request_id;
  output = std::move(decoded);
  return gneiss::engine::error::success;
}

std::span<const gneiss::engine::ipc_operation_descriptor> ipc_property_operations() noexcept {
  return property_operations;
}

} // namespace gneiss::host
