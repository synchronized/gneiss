// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/guid.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/resource/scene/scene_record.hpp"

#include "engine/function/scene/scene_types.hpp"

#include <string_view>

namespace gneiss::engine {

/** 创建调用期间借用字符串；成功后场景独立拥有运行数据。 */
struct node_creation {
  guid uuid;
  std::string_view name;
  entity_id parent{};
  math::transform local_transform = {};
};

} // namespace gneiss::engine
