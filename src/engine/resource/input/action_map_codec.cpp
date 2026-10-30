// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "action_map_codec.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include "engine/resource/detail/reflected_asset_codec.hpp"
#include <algorithm>
#include <cmath>
namespace gneiss::engine {
namespace {
using diagnostic = meta::serialization_diagnostic;
diagnostic invalid(std::string path, std::string message = {}) {
  return {.path = std::move(path), .message = std::move(message), .error = error::invalid_argument};
}
diagnostic validate_binding(const action_binding& binding) {
  if (binding.physical_key == 0 || binding.physical_key >= 256 || !std::isfinite(binding.scale) ||
      binding.scale == 0 || binding.scale < -1 || binding.scale > 1)
    return invalid("", "按键或 scale 超出有效范围");
  return {};
}
constexpr detail::reflected_asset_format file_format{
    "gneiss.input-map", 1, maximum_action_map_bytes, error::invalid_argument};
result<action_map, diagnostic> decode(std::span<const std::byte> bytes) {
  return detail::decode_reflected_asset<action_map>(bytes, file_format);
}
result<std::vector<std::byte>, diagnostic> encode(const action_map& object) {
  return detail::encode_reflected_asset(object, file_format);
}
}
error register_action_map_reflection() noexcept {
  constexpr auto required = meta::property_flags::required;
  auto status = meta::type_builder<action_binding>{}
                    .property<&action_binding::physical_key>(meta::property_id{1}, "key", required)
                    .property<&action_binding::scale>(meta::property_id{2}, "scale", required)
                    .serialization(true)
                    .validate<&validate_binding>()
                    .commit();
  if (status.failed())
    return status;
  status = meta::type_builder<action_definition>{}
               .property<&action_definition::name>(meta::property_id{1}, "name", required)
               .property<&action_definition::bindings>(meta::property_id{2}, "bindings", required)
               .serialization(true)
               .commit();
  if (status.failed())
    return status;
  return meta::type_builder<action_map>{}
      .schema_version(1)
      .property<&action_map::actions>(meta::property_id{1}, "actions", required)
      .serialization(true)
      .serialization<&encode, &decode>()
      .validate<&validate_action_map>()
      .commit();
}
result<action_map, diagnostic> parse_action_map(std::string_view json) noexcept {
  return decode_action_map(std::as_bytes(std::span{json.data(), json.size()}));
}
result<action_map, diagnostic> decode_action_map(std::span<const std::byte> bytes) noexcept {
  return meta::deserialize<action_map>(bytes);
}
result<std::vector<std::byte>, diagnostic> encode_action_map(const action_map& object) noexcept {
  return meta::serialize(object);
}
}
