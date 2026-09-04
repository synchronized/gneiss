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
  std::vector<std::uint8_t> payload;
  gneiss::ipc_property_write decoded;
  return gneiss::encode_ipc_property_write(source, payload) == gneiss::result::success &&
         std::string(payload.begin(), payload.end()).find("command_id") == std::string::npos &&
         gneiss::decode_ipc_property_write(payload, decoded) == gneiss::result::success &&
         decoded.session_id == source.session_id && decoded.command_id == 0U &&
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
  std::vector<std::uint8_t> payload;
  gneiss::ipc_property_write_result decoded;
  if (gneiss::encode_ipc_property_write_result(success, payload) != gneiss::result::success ||
      std::string(payload.begin(), payload.end()).find("command_id") != std::string::npos ||
      gneiss::decode_ipc_property_write_result(payload, decoded) != gneiss::result::success ||
      decoded.session_id != success.session_id || decoded.command_id != 0U || decoded.code != 0 ||
      decoded.revision != success.revision || decoded.message != success.message ||
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
  return gneiss::encode_ipc_property_write_result(rejected, payload) == gneiss::result::success &&
         gneiss::decode_ipc_property_write_result(payload, decoded) == gneiss::result::success &&
         decoded.code == rejected.code && decoded.revision == rejected.revision &&
         std::holds_alternative<std::monostate>(decoded.canonical_value.payload);
}

bool test_invalid_messages() {
  gneiss::ipc_property_write invalid;
  std::vector<std::uint8_t> payload;
  if (gneiss::encode_ipc_property_write(invalid, payload) != gneiss::result::invalid_argument) {
    return false;
  }

  invalid = {.session_id = 1U,
             .command_id = 1U,
             .object = {1U, 1U},
             .type_id = test_type_id(),
             .field_id = 1U,
             .expected_revision = 1U,
             .value = {std::numeric_limits<float>::infinity()}};
  if (gneiss::encode_ipc_property_write(invalid, payload) != gneiss::result::invalid_argument) {
    return false;
  }

  payload = {'{', '}'};
  gneiss::ipc_property_write decoded;
  if (gneiss::decode_ipc_property_write(payload, decoded) != gneiss::result::invalid_argument) {
    return false;
  }
  payload = {'{'};
  if (gneiss::decode_ipc_property_write(payload, decoded) != gneiss::result::invalid_argument) {
    return false;
  }
  return true;
}

} // namespace

int main() { return test_all_value_types() && test_results() && test_invalid_messages() ? 0 : 1; }
