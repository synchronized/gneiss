// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_property_edit_protocol.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

gneiss_type_id test_type_id() noexcept {
  gneiss_type_id id{};
  for (std::uint8_t index = 0U; index < 16U; ++index) {
    id.bytes[index] = static_cast<std::uint8_t>(index + 1U);
  }
  return id;
}

bool round_trip_value(const gneiss::ipc_property_value& value) {
  const gneiss::ipc_property_write source{.session_id = 9U,
                                          .command_id = 21U,
                                          .object = {5U, 2U},
                                          .type_id = test_type_id(),
                                          .field_id = 7U,
                                          .expected_revision = 11U,
                                          .value = value};
  gneiss::ipc_frame frame;
  gneiss::ipc_property_write decoded;
  return gneiss::encode_ipc_property_write(source, frame) == gneiss::result::success &&
         frame.protocol_minor == gneiss::ipc_protocol_minor &&
         frame.message_type ==
             static_cast<std::uint16_t>(gneiss::ipc_message_type::property_write) &&
         gneiss::decode_ipc_property_write(frame, decoded) == gneiss::result::success &&
         decoded.session_id == source.session_id && decoded.command_id == source.command_id &&
         decoded.object == source.object && decoded.field_id == source.field_id &&
         decoded.expected_revision == source.expected_revision &&
         decoded.value.payload == value.payload;
}

bool test_all_value_types() {
  const std::vector<gneiss::ipc_property_value> values{
      {true},
      {std::int64_t{-42}},
      {std::uint64_t{42}},
      {1.25F},
      {2.5},
      {std::string{"运行态值"}},
      {std::array<std::uint8_t, 16>{1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U,
                                    15U, 16U}},
      {std::array<float, 3>{1.0F, 2.0F, 3.0F}},
      {std::array<float, 4>{0.0F, 0.0F, 0.0F, 1.0F}},
  };
  for (const auto& value : values) {
    if (!round_trip_value(value)) {
      return false;
    }
  }
  return true;
}

bool test_results() {
  gneiss::ipc_property_write_result success{
      .session_id = 9U,
      .command_id = 21U,
      .code = 0,
      .revision = 12U,
      .message = "已应用",
      .canonical_value = {std::array<float, 3>{1.0F, 2.0F, 3.0F}}};
  gneiss::ipc_frame frame;
  gneiss::ipc_property_write_result decoded;
  if (gneiss::encode_ipc_property_write_result(success, frame) != gneiss::result::success ||
      gneiss::decode_ipc_property_write_result(frame, decoded) != gneiss::result::success ||
      decoded.session_id != success.session_id || decoded.command_id != success.command_id ||
      decoded.code != 0 || decoded.revision != success.revision ||
      decoded.message != success.message ||
      decoded.canonical_value.payload != success.canonical_value.payload) {
    return false;
  }

  const gneiss::ipc_property_write_result rejected{.session_id = 9U,
                                                   .command_id = 22U,
                                                   .code = -2,
                                                   .revision = 12U,
                                                   .message = "修订冲突",
                                                   .canonical_value = {}};
  decoded = {};
  return gneiss::encode_ipc_property_write_result(rejected, frame) == gneiss::result::success &&
         gneiss::decode_ipc_property_write_result(frame, decoded) == gneiss::result::success &&
         decoded.code == rejected.code && decoded.revision == rejected.revision &&
         std::holds_alternative<std::monostate>(decoded.canonical_value.payload);
}

bool test_invalid_messages() {
  gneiss::ipc_property_write invalid;
  gneiss::ipc_frame frame;
  if (gneiss::encode_ipc_property_write(invalid, frame) != gneiss::result::invalid_argument) {
    return false;
  }

  invalid = {.session_id = 1U,
             .command_id = 1U,
             .object = {1U, 1U},
             .type_id = test_type_id(),
             .field_id = 1U,
             .expected_revision = 1U,
             .value = {std::numeric_limits<float>::infinity()}};
  if (gneiss::encode_ipc_property_write(invalid, frame) != gneiss::result::invalid_argument) {
    return false;
  }

  frame = {.protocol_major = gneiss::ipc_protocol_major,
           .protocol_minor = gneiss::ipc_protocol_minor,
           .message_type = static_cast<std::uint16_t>(gneiss::ipc_message_type::property_write),
           .payload = {'{', '}'}};
  gneiss::ipc_property_write decoded;
  if (gneiss::decode_ipc_property_write(frame, decoded) != gneiss::result::invalid_argument) {
    return false;
  }
  frame.payload = {'{'};
  if (gneiss::decode_ipc_property_write(frame, decoded) != gneiss::result::invalid_argument) {
    return false;
  }
  ++frame.protocol_major;
  return gneiss::decode_ipc_property_write(frame, decoded) == gneiss::result::unsupported;
}

bool test_capability_negotiation() {
  const std::vector<std::string> requested{
      std::string(gneiss::ipc_capability_runtime_inspection_v1),
      std::string(gneiss::ipc_capability_runtime_property_edit_v1)};
  const std::vector<std::string> supported{
      std::string(gneiss::ipc_capability_runtime_property_edit_v1)};
  gneiss::ipc_frame hello;
  gneiss::ipc_frame acknowledgment;
  std::vector<std::string> negotiated;
  return gneiss::make_ipc_hello("token", requested, hello) == gneiss::result::success &&
         gneiss::accept_ipc_hello(hello, "token", supported, acknowledgment, negotiated) ==
             gneiss::result::success &&
         negotiated == supported;
}

} // namespace

int main() {
  return test_all_value_types() && test_results() && test_invalid_messages() &&
                 test_capability_negotiation()
             ? 0
             : 1;
}
