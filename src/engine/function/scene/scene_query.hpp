// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/guid.hpp"
#include "engine/core/math/transform.hpp"

#include "engine/resource/scene/scene_description.hpp"

#include "engine/core/entity_id.hpp"
#include "engine/core/scene_types.hpp"

#include "engine/resource/scene/prefab_property_data.hpp"
#include <optional>
#include <span>
#include <string_view>

namespace gneiss::engine {

enum class node_query_detail { identity, assets, components };

/** Prefab 来源关系；不重复节点、实体或运行时变换。 */
struct prefab_origin_view {
  bool is_instance_root{};
  guid instance_uuid;
  guid source_node_uuid;
  std::string_view prefab_uri;
  math::transform source_local_transform{};
  /** 借用实例覆盖列表，有效期到下次场景修改；查询自动筛选当前源节点。 */
  std::span<const prefab_property_override> overrides;
  [[nodiscard]] bool has_override(guid type, meta::property_id property) const noexcept {
    for (const auto& item : overrides)
      if (item.key.source_node_uuid == source_node_uuid && item.key.type_guid == type &&
          item.key.property_id == property)
        return true;
    return false;
  }
};

/** 字符串借用到下次场景修改或销毁；查询不延长场景寿命。 */
struct scene_node_view {
  scene_node_id node{};
  scene_node_id parent{};
  entity_id entity{};
  guid uuid;
  std::string_view name;
  std::string_view mesh_uri;
  std::string_view material_uri;
  math::transform local_transform = {};
  scene_node_components component_flags{};
  std::optional<camera_description> camera;
  std::optional<prefab_origin_view> prefab;
};

} // namespace gneiss::engine
