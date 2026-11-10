// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/guid.hpp"
#include "engine/core/math/transform.hpp"

#include "engine/resource/scene/component_description.hpp"

#include "engine/core/entity_id.hpp"
#include "engine/function/scene/scene_types.hpp"

#include <string>

namespace gneiss::test_support {
using engine::component_descriptions;
using engine::guid;
namespace math = engine::math;

enum class node_query_detail { identity, components };

/** 名称与组件集合是自有只读快照。
 * 查询不延长场景寿命。 */
struct scene_node_info {
  entity_id node{};
  entity_id parent{};
  entity_id entity{};
  guid uuid;
  std::string name;
  math::transform local_transform = {};
  component_descriptions components{};
};

} // namespace gneiss::test_support
