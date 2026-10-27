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
  if (left.node.instance_uuid != right.node.instance_uuid) {
    return left.node.instance_uuid < right.node.instance_uuid;
  }
  if (left.node.source_node_uuid != right.node.source_node_uuid) {
    return left.node.source_node_uuid < right.node.source_node_uuid;
  }
  if (!std::ranges::equal(left.type_id.bytes(), right.type_id.bytes())) {
    return std::lexicographical_compare(
        std::begin(left.type_id.bytes()), std::end(left.type_id.bytes()),
        std::begin(right.type_id.bytes()), std::end(right.type_id.bytes()));
  }
  return left.field_id.get() < right.field_id.get();
}

bool prefab_property_override_key::operator==(
    const prefab_property_override_key& other) const noexcept {
  return node == other.node && field_id == other.field_id &&
         std::ranges::equal(type_id.bytes(), other.type_id.bytes());
}

value_kind prefab_property_value_kind(const prefab_property_value& value) noexcept {
  return value.payload.valueless_by_exception() ? value_kind::invalid
                                                : static_cast<value_kind>(value.payload.index());
}

} // namespace gneiss::engine
