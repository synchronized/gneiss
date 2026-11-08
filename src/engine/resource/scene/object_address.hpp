// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/guid.hpp"
namespace gneiss::engine {
/** 文档内地址；普通对象 source 为 nil，Prefab 成员由实例与来源 UUID 联合定位。 */
struct object_address {
  guid object, source;
  auto operator<=>(const object_address&) const = default;
};
}
