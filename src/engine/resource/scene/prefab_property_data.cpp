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
  return !address.instance_uuid.is_nil() && !address.source_node_uuid.is_nil();
}

bool prefab_property_override_key_less(const prefab_property_override_key& left,
                                       const prefab_property_override_key& right) noexcept {
  if (left.target.source_node_uuid != right.target.source_node_uuid)
    return left.target.source_node_uuid < right.target.source_node_uuid;
  if (left.target.target_type_guid != right.target.target_type_guid)
    return left.target.target_type_guid < right.target.target_type_guid;
  return std::lexicographical_compare(left.path.begin(), left.path.end(), right.path.begin(),
                                      right.path.end(), meta::property_step_less);
}

} // namespace gneiss::engine
