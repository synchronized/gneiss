// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_property_edit_protocol.h"

#include <yyjson.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

constexpr std::size_t max_message_size = 1024U;
constexpr std::size_t max_property_string_size = 16U * 1024U;
constexpr char hex_digits[] = "0123456789abcdef";

using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;

bool valid_type_id(gneiss_type_id id) noexcept {
  return std::ranges::any_of(id.bytes, [](std::uint8_t value) { return value != 0U; });
}

std::string type_id_text(gneiss_type_id id) {
  std::string text(32U, '0');
  for (std::size_t index = 0U; index < 16U; ++index) {
    text[index * 2U] = hex_digits[id.bytes[index] >> 4U];
    text[index * 2U + 1U] = hex_digits[id.bytes[index] & 0x0FU];
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

bool parse_type_id(yyjson_val* value, gneiss_type_id& output) noexcept {
  if (!yyjson_is_str(value) || yyjson_get_len(value) != 32U) {
    return false;
  }
  gneiss_type_id parsed{};
  const auto* text = yyjson_get_str(value);
  for (std::size_t index = 0U; index < 16U; ++index) {
    const auto high = hex_value(text[index * 2U]);
    const auto low = hex_value(text[index * 2U + 1U]);
    if (high < 0 || low < 0) {
      return false;
    }
    parsed.bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  if (!valid_type_id(parsed)) {
    return false;
  }
  output = parsed;
  return true;
}

template <std::size_t Size>
bool add_float_array(yyjson_mut_doc* document, yyjson_mut_val* root,
                     const std::array<float, Size>& values) noexcept {
  auto* array = yyjson_mut_arr(document);
  if (array == nullptr) {
    return false;
  }
  for (const auto value : values) {
    if (!std::isfinite(value) || !yyjson_mut_arr_add_real(document, array, value)) {
      return false;
    }
  }
  return yyjson_mut_obj_add_val(document, root, "value", array);
}

bool add_property_value(yyjson_mut_doc* document, yyjson_mut_val* root,
                        const gneiss::ipc_property_value& value) {
  const auto add_kind = [&](const char* kind) {
    return yyjson_mut_obj_add_str(document, root, "kind", kind);
  };
  return std::visit(
      [&](const auto& payload) -> bool {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, std::monostate>) {
          return false;
        } else if constexpr (std::is_same_v<Payload, bool>) {
          return add_kind("bool") && yyjson_mut_obj_add_bool(document, root, "value", payload);
        } else if constexpr (std::is_same_v<Payload, std::int64_t>) {
          return add_kind("int64") && yyjson_mut_obj_add_sint(document, root, "value", payload);
        } else if constexpr (std::is_same_v<Payload, std::uint64_t>) {
          return add_kind("uint64") && yyjson_mut_obj_add_uint(document, root, "value", payload);
        } else if constexpr (std::is_same_v<Payload, float>) {
          return std::isfinite(payload) && add_kind("float32") &&
                 yyjson_mut_obj_add_real(document, root, "value", payload);
        } else if constexpr (std::is_same_v<Payload, double>) {
          return std::isfinite(payload) && add_kind("float64") &&
                 yyjson_mut_obj_add_real(document, root, "value", payload);
        } else if constexpr (std::is_same_v<Payload, std::string>) {
          return payload.size() <= max_property_string_size && add_kind("string") &&
                 yyjson_mut_obj_add_strncpy(document, root, "value", payload.data(),
                                            payload.size());
        } else if constexpr (std::is_same_v<Payload, std::array<std::uint8_t, 16>>) {
          gneiss_type_id id{};
          std::ranges::copy(payload, id.bytes);
          if (!valid_type_id(id) || !add_kind("type_id")) {
            return false;
          }
          const auto text = type_id_text(id);
          return yyjson_mut_obj_add_strncpy(document, root, "value", text.data(), text.size());
        } else if constexpr (std::is_same_v<Payload, std::array<float, 3>>) {
          return add_kind("vec3") && add_float_array(document, root, payload);
        } else {
          return add_kind("quaternion") && add_float_array(document, root, payload);
        }
      },
      value.payload);
}

template <std::size_t Size>
bool parse_float_array(yyjson_val* value, std::array<float, Size>& output) noexcept {
  if (!yyjson_is_arr(value) || yyjson_arr_size(value) != Size) {
    return false;
  }
  std::array<float, Size> parsed{};
  std::size_t index = 0U;
  std::size_t count = 0U;
  yyjson_val* item = nullptr;
  yyjson_arr_foreach(value, index, count, item) {
    if (!yyjson_is_num(item)) {
      return false;
    }
    const auto number = yyjson_get_real(item);
    if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
        number > std::numeric_limits<float>::max()) {
      return false;
    }
    parsed[index] = static_cast<float>(number);
  }
  output = parsed;
  return true;
}

bool parse_property_value(yyjson_val* root, gneiss::ipc_property_value& output) {
  auto* kind_value = yyjson_is_obj(root) ? yyjson_obj_get(root, "kind") : nullptr;
  auto* value = yyjson_is_obj(root) ? yyjson_obj_get(root, "value") : nullptr;
  if (!yyjson_is_str(kind_value)) {
    return false;
  }
  const std::string_view kind(yyjson_get_str(kind_value), yyjson_get_len(kind_value));
  gneiss::ipc_property_value parsed;
  if (kind == "bool" && yyjson_is_bool(value)) {
    parsed.payload = yyjson_get_bool(value);
  } else if (kind == "int64" && yyjson_is_sint(value)) {
    parsed.payload = yyjson_get_sint(value);
  } else if (kind == "uint64" && yyjson_is_uint(value)) {
    parsed.payload = yyjson_get_uint(value);
  } else if (kind == "float32" && yyjson_is_num(value)) {
    const auto number = yyjson_get_real(value);
    if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
        number > std::numeric_limits<float>::max()) {
      return false;
    }
    parsed.payload = static_cast<float>(number);
  } else if (kind == "float64" && yyjson_is_num(value)) {
    const auto number = yyjson_get_real(value);
    if (!std::isfinite(number)) {
      return false;
    }
    parsed.payload = number;
  } else if (kind == "string" && yyjson_is_str(value) &&
             yyjson_get_len(value) <= max_property_string_size) {
    parsed.payload = std::string(yyjson_get_str(value), yyjson_get_len(value));
  } else if (kind == "type_id") {
    gneiss_type_id id{};
    if (!parse_type_id(value, id)) {
      return false;
    }
    std::array<std::uint8_t, 16> bytes{};
    std::ranges::copy(id.bytes, bytes.begin());
    parsed.payload = bytes;
  } else if (kind == "vec3") {
    std::array<float, 3> numbers{};
    if (!parse_float_array(value, numbers)) {
      return false;
    }
    parsed.payload = numbers;
  } else if (kind == "quaternion") {
    std::array<float, 4> numbers{};
    if (!parse_float_array(value, numbers)) {
      return false;
    }
    parsed.payload = numbers;
  } else {
    return false;
  }
  output = std::move(parsed);
  return true;
}

bool add_common(yyjson_mut_doc* document, yyjson_mut_val* root, std::uint64_t session_id,
                std::uint64_t command_id) noexcept {
  return session_id != 0U && command_id != 0U &&
         yyjson_mut_obj_add_uint(document, root, "session_id", session_id) &&
         yyjson_mut_obj_add_uint(document, root, "command_id", command_id);
}

bool parse_uint(yyjson_val* root, const char* name, std::uint64_t& output) noexcept {
  auto* value = yyjson_is_obj(root) ? yyjson_obj_get(root, name) : nullptr;
  if (!yyjson_is_uint(value)) {
    return false;
  }
  output = yyjson_get_uint(value);
  return true;
}

gneiss::result write_frame(yyjson_mut_doc* document, yyjson_mut_val* root,
                           gneiss::ipc_message_type type, gneiss::ipc_frame& output) {
  yyjson_mut_doc_set_root(document, root);
  std::size_t length = 0U;
  std::unique_ptr<char, decltype(&std::free)> json(
      yyjson_mut_write(document, YYJSON_WRITE_NOFLAG, &length), &std::free);
  if (!json || length > gneiss::ipc_protocol_max_json_size) {
    return json ? gneiss::result::invalid_argument : gneiss::result::out_of_memory;
  }
  gneiss::ipc_frame frame;
  frame.protocol_major = gneiss::ipc_protocol_major;
  frame.protocol_minor = gneiss::ipc_protocol_minor;
  frame.message_type = static_cast<std::uint16_t>(type);
  frame.payload.assign(reinterpret_cast<const std::uint8_t*>(json.get()),
                       reinterpret_cast<const std::uint8_t*>(json.get()) + length);
  output = std::move(frame);
  return gneiss::result::success;
}

bool supports_frame(const gneiss::ipc_frame& frame, gneiss::ipc_message_type type) noexcept {
  return frame.protocol_major == gneiss::ipc_protocol_major &&
         frame.message_type == static_cast<std::uint16_t>(type) &&
         frame.payload.size() <= gneiss::ipc_protocol_max_json_size;
}

} // namespace

namespace gneiss {

result encode_ipc_property_write(const ipc_property_write& command, ipc_frame& output) noexcept {
  if (!command.object.is_valid() || !valid_type_id(command.type_id) ||
      command.field_id == GNEISS_NULL_FIELD_ID || command.expected_revision == 0U) {
    return result::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    auto* value = document ? yyjson_mut_obj(document.get()) : nullptr;
    const auto type_text = type_id_text(command.type_id);
    if (root == nullptr || value == nullptr ||
        !add_common(document.get(), root, command.session_id, command.command_id) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "object_id", command.object.value) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "generation", command.object.generation) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "type_id", type_text.data(),
                                    type_text.size()) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "field_id", command.field_id) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "expected_revision",
                                 command.expected_revision) ||
        !add_property_value(document.get(), value, command.value) ||
        !yyjson_mut_obj_add_val(document.get(), root, "value", value)) {
      return result::invalid_argument;
    }
    return write_frame(document.get(), root, ipc_message_type::property_write, output);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_property_write(const ipc_frame& frame, ipc_property_write& output) noexcept {
  if (!supports_frame(frame, ipc_message_type::property_write)) {
    return result::unsupported;
  }
  try {
    document_ptr document(yyjson_read(reinterpret_cast<const char*>(frame.payload.data()),
                                      frame.payload.size(), YYJSON_READ_NOFLAG),
                          &yyjson_doc_free);
    if (!document) {
      return result::invalid_argument;
    }
    auto* root = yyjson_doc_get_root(document.get());
    ipc_property_write parsed;
    std::uint64_t generation = 0U;
    std::uint64_t field_id = 0U;
    if (!parse_uint(root, "session_id", parsed.session_id) ||
        !parse_uint(root, "command_id", parsed.command_id) ||
        !parse_uint(root, "object_id", parsed.object.value) ||
        !parse_uint(root, "generation", generation) ||
        generation > std::numeric_limits<std::uint32_t>::max() ||
        !parse_type_id(yyjson_obj_get(root, "type_id"), parsed.type_id) ||
        !parse_uint(root, "field_id", field_id) ||
        field_id > std::numeric_limits<std::uint32_t>::max() ||
        !parse_uint(root, "expected_revision", parsed.expected_revision) ||
        !parse_property_value(yyjson_obj_get(root, "value"), parsed.value)) {
      return result::invalid_argument;
    }
    parsed.object.generation = static_cast<std::uint32_t>(generation);
    parsed.field_id = static_cast<gneiss_field_id>(field_id);
    if (parsed.session_id == 0U || parsed.command_id == 0U || !parsed.object.is_valid() ||
        parsed.field_id == GNEISS_NULL_FIELD_ID || parsed.expected_revision == 0U) {
      return result::invalid_argument;
    }
    output = std::move(parsed);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result encode_ipc_property_write_result(const ipc_property_write_result& response,
                                        ipc_frame& output) noexcept {
  if (response.message.size() > max_message_size ||
      (response.code == 0 && (response.revision == 0U || std::holds_alternative<std::monostate>(
                                                             response.canonical_value.payload)))) {
    return result::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    if (root == nullptr ||
        !add_common(document.get(), root, response.session_id, response.command_id) ||
        !yyjson_mut_obj_add_sint(document.get(), root, "code", response.code) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "revision", response.revision) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "message", response.message.data(),
                                    response.message.size())) {
      return result::invalid_argument;
    }
    if (response.code == 0) {
      auto* value = yyjson_mut_obj(document.get());
      if (value == nullptr ||
          !add_property_value(document.get(), value, response.canonical_value) ||
          !yyjson_mut_obj_add_val(document.get(), root, "value", value)) {
        return result::invalid_argument;
      }
    }
    return write_frame(document.get(), root, ipc_message_type::property_write_result, output);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_property_write_result(const ipc_frame& frame,
                                        ipc_property_write_result& output) noexcept {
  if (!supports_frame(frame, ipc_message_type::property_write_result)) {
    return result::unsupported;
  }
  try {
    document_ptr document(yyjson_read(reinterpret_cast<const char*>(frame.payload.data()),
                                      frame.payload.size(), YYJSON_READ_NOFLAG),
                          &yyjson_doc_free);
    if (!document) {
      return result::invalid_argument;
    }
    auto* root = yyjson_doc_get_root(document.get());
    ipc_property_write_result parsed;
    auto* code = yyjson_is_obj(root) ? yyjson_obj_get(root, "code") : nullptr;
    auto* message = yyjson_is_obj(root) ? yyjson_obj_get(root, "message") : nullptr;
    if (!parse_uint(root, "session_id", parsed.session_id) ||
        !parse_uint(root, "command_id", parsed.command_id) || !yyjson_is_int(code) ||
        yyjson_get_sint(code) < std::numeric_limits<std::int32_t>::min() ||
        yyjson_get_sint(code) > std::numeric_limits<std::int32_t>::max() ||
        !parse_uint(root, "revision", parsed.revision) || !yyjson_is_str(message) ||
        yyjson_get_len(message) > max_message_size) {
      return result::invalid_argument;
    }
    parsed.code = static_cast<std::int32_t>(yyjson_get_sint(code));
    parsed.message.assign(yyjson_get_str(message), yyjson_get_len(message));
    if (parsed.session_id == 0U || parsed.command_id == 0U ||
        (parsed.code == 0 &&
         (parsed.revision == 0U ||
          !parse_property_value(yyjson_obj_get(root, "value"), parsed.canonical_value)))) {
      return result::invalid_argument;
    }
    output = std::move(parsed);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

} // namespace gneiss
