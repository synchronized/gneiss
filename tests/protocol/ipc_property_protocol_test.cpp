// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/meta/object_type.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/ipc_property_protocol.hpp>
#include <gneiss/protocol/registration.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace gneiss::host;

namespace {
namespace e = gneiss;
namespace m = e::meta;
struct wire_object : m::object_type<wire_object> {
  static constexpr std::string_view type_name = "wire_object";
  static constexpr e::guid type_guid = e::guid::from_bytes({94, 27});
  int count = 9;
};
bool test_object_value() {
  if (gneiss::initialize_meta([] {
        auto status = m::registration::type<wire_object>()
                          .property<&wire_object::count>(gneiss::property_id{1}, "count")
                          .commit();
        return status.failed() ? status : gneiss::protocol::register_types();
      }).failed())
    return false;
  ipc_property_write command{.session_id = 1,
                             .object = {1, 1},
                             .type_id = wire_object::type_guid,
                             .field_id = 1,
                             .expected_revision = 1,
                             .value = {e::ref<m::object>{e::make_ref<wire_object>()}}};
  auto bytes = e::protocol::encode(property_write, command, 1);
  if (!bytes)
    return false;
  auto restored = e::protocol::decode(property_write, *bytes);
  if (!restored)
    return false;
  auto original = std::get<e::ref<m::object>>(command.value.payload);
  auto decoded = std::get<e::ref<m::object>>(restored->value.payload);
  auto* object = dynamic_cast<wire_object*>(decoded.get());
  if (!object || object->count != 9 || original == decoded)
    return false;
  command.value = e::value{e::ref<m::object>{}};
  bytes = e::protocol::encode(property_write, command, 1);
  if (!bytes)
    return false;
  restored = e::protocol::decode(property_write, *bytes);
  if (!restored || restored->value != command.value)
    return false;
  return true;
}

gneiss::guid test_type_id() noexcept {
  gneiss::guid::byte_array bytes{};
  for (std::uint8_t index = 0U; index < 16U; ++index) {
    bytes[index] = static_cast<std::uint8_t>(index + 1U);
  }
  return gneiss::guid::from_bytes(bytes);
}

bool round_trip_value(const gneiss::value& value) {
  const gneiss::host::ipc_property_write source{.session_id = 9U,
                                                .command_id = 21U,
                                                .object = {5U, 2U},
                                                .type_id = test_type_id(),
                                                .field_id = 7U,
                                                .expected_revision = 11U,
                                                .value = value};
  auto payload = e::protocol::encode(property_write, source, 21);
  if (!payload)
    return false;
  auto decoded = e::protocol::decode(property_write, *payload);
  return decoded && decoded->command_id == 0 && decoded->object == source.object &&
         decoded->type_id == source.type_id && decoded->session_id == source.session_id &&
         decoded->field_id == source.field_id &&
         decoded->expected_revision == source.expected_revision && decoded->value == value &&
         std::string(payload->payload.begin(), payload->payload.end()).find("command_id") ==
             std::string::npos;
}

bool test_all_value_types() {
  const std::vector<gneiss::value> values{
      {},
      {e::guid{}},
      {e::ref<m::object>{}},
      {e::math::matrix3{2}},
      {e::math::matrix4{3}},
      {e::math::vector2{1, 2}},
      {e::math::vector4{1, 2, 3, 4}},
      {true},
      {std::int64_t{-42}},
      {std::uint64_t{42}},
      {1.25F},
      {2.5},
      {std::string{"运行态值"}},
      {gneiss::guid::from_bytes(
          {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U})},
      {gneiss::math::vector3{1.0F, 2.0F, 3.0F}},
      {gneiss::math::quaternion_xyzw(0.0F, 0.0F, 0.0F, 1.0F)},
  };
  for (const auto& value : values) {
    if (!round_trip_value(value)) {
      return false;
    }
  }
  return true;
}

bool test_structured_values() {
  const std::vector<e::value> values{
      {e::sequence_value{
          e::sequence_value_data{{e::value{true}, e::value{std::string{"nested"}}}}}},
      {e::optional_value{e::optional_value_data{e::value{std::int64_t{42}}}}},
      {e::variant_value{e::variant_value_data{"choice", e::value{false}}}},
      {e::map_value{e::map_value_data{{{e::value{std::string{"key"}}, e::value{true}}}}}},
      {e::record_value{e::record_value_data{wire_object::type_guid, {{1, e::value{true}}}}}},
      {e::asset_reference_value{wire_object::type_guid, "asset://materials/main.material"}},
      {e::object_reference_value{wire_object::type_guid, wire_object::type_guid}},
  };
  for (const auto& value : values)
    if (!round_trip_value(value))
      return false;
  return true;
}

bool test_results() {
  ipc_property_write_result response{.session_id = 9,
                                     .command_id = 21,
                                     .revision = 12,
                                     .message = "已应用",
                                     .canonical_value = {true}};
  auto envelope = e::protocol::encode(property_result, response, 21);
  if (!envelope)
    return false;
  auto decoded = e::protocol::decode(property_result, *envelope);
  if (!decoded || decoded->canonical_value != response.canonical_value || decoded->revision != 12)
    return false;
  response.canonical_value = {};
  if (e::protocol::encode(property_result, response, 21))
    return false;
  response.code = e::error::invalid_state.code();
  response.revision = 0;
  envelope = e::protocol::encode(property_result, response, 21);
  if (!envelope)
    return false;
  decoded = e::protocol::decode(property_result, *envelope);
  return decoded && decoded->code == response.code && decoded->revision == 0;
}

bool test_invalid_messages() {
  ipc_property_write command{.session_id = 1,
                             .object = {1, 1},
                             .type_id = test_type_id(),
                             .field_id = 1,
                             .expected_revision = 1,
                             .value = {true}};
  if (e::protocol::encode(property_write, command, 0))
    return false;
  auto envelope = e::protocol::encode(property_write, command, 1);
  if (!envelope)
    return false;
  auto invalid = *envelope;
  invalid.kind = e::ipc::message_kind::response;
  if (e::protocol::decode(property_write, invalid))
    return false;
  for (const std::string text : {"{}", "{"}) {
    invalid = *envelope;
    invalid.payload.assign(text.begin(), text.end());
    if (e::protocol::decode(property_write, invalid))
      return false;
  }
  invalid.payload.resize(65537);
  if (e::protocol::decode(property_write, invalid))
    return false;
  command.value = {std::numeric_limits<float>::infinity()};
  if (e::protocol::encode(property_write, command, 1))
    return false;
  command.value = {std::string(16385, 'a')};
  if (e::protocol::encode(property_write, command, 1))
    return false;
  command.value = {true};
  command.object.generation = 0;
  return !e::protocol::encode(property_write, command, 1);
}

} // namespace

int main() {
  // 与宿主一致，在编解码前完成协议与对象类型注册。
  if (!test_object_value())
    return 90;
  if (!test_all_value_types())
    return 91;
  return test_structured_values() && test_results() && test_invalid_messages() ? 0 : 1;
}
