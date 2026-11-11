// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/guid.hpp"
namespace gneiss {
/** 文档内地址；普通对象 source 为 nil，Prefab 成员由实例与来源 UUID 联合定位。 */
struct object_address {
  static constexpr std::string_view type_name = "object_address";
  static constexpr guid type_guid = guid::literal("a0f2372b-e17b-416e-9606-c5dc4ad9f8dc");
  guid object, source;
  auto operator<=>(const object_address&) const = default;
};
}
