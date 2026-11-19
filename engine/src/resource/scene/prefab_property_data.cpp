// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/resource/scene/prefab_property_data.hpp>
#include <algorithm>

namespace gneiss {
bool prefab_property_override_key_less(const prefab_property_override_key& left,
                                       const prefab_property_override_key& right) noexcept {
  if (left.target.source_node_uuid != right.target.source_node_uuid)
    return left.target.source_node_uuid < right.target.source_node_uuid;
  if (left.target.target_type_guid != right.target.target_type_guid)
    return left.target.target_type_guid < right.target.target_type_guid;
  if (left.target.component_uuid != right.target.component_uuid)
    return left.target.component_uuid < right.target.component_uuid;
  return std::lexicographical_compare(left.path.begin(), left.path.end(), right.path.begin(),
                                      right.path.end(), gneiss::property_step_less);
}

} // namespace gneiss
