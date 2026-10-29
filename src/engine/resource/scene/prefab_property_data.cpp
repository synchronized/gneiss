// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/scene/prefab_property_data.hpp"
#include <algorithm>

namespace {
[[nodiscard]] bool is_canonical_uuid_impl(std::string_view value) noexcept {
  if (value.size() != 36U) {
    return false;
  }
  for (std::size_t index = 0U; index < value.size(); ++index) {
    if (index == 8U || index == 13U || index == 18U || index == 23U) {
      if (value[index] != '-') {
        return false;
      }
    } else if ((value[index] < '0' || value[index] > '9') &&
               (value[index] < 'a' || value[index] > 'f')) {
      return false;
    }
  }
  return true;
}

}

namespace gneiss::engine {
bool is_canonical_prefab_uuid(std::string_view value) noexcept {
  return is_canonical_uuid_impl(value);
}

bool is_valid_prefab_author_address(const prefab_author_address& address) noexcept {
  return is_canonical_prefab_uuid(address.instance_uuid) &&
         is_canonical_prefab_uuid(address.source_node_uuid);
}

bool prefab_property_override_key_less(const prefab_property_override_key& left,
                                       const prefab_property_override_key& right) noexcept {
  if (left.source_node_uuid != right.source_node_uuid) {
    return left.source_node_uuid < right.source_node_uuid;
  }
  if (!std::ranges::equal(left.type_guid.bytes(), right.type_guid.bytes())) {
    return std::lexicographical_compare(
        std::begin(left.type_guid.bytes()), std::end(left.type_guid.bytes()),
        std::begin(right.type_guid.bytes()), std::end(right.type_guid.bytes()));
  }
  return left.property_id.get() < right.property_id.get();
}

bool prefab_property_override_key::operator==(
    const prefab_property_override_key& other) const noexcept {
  return source_node_uuid == other.source_node_uuid && property_id == other.property_id &&
         std::ranges::equal(type_guid.bytes(), other.type_guid.bytes());
}

} // namespace gneiss::engine
