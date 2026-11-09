// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/ops/object_ops.hpp"
#include "engine/core/meta/property.hpp"
#include "engine/core/meta/type_types.hpp"
namespace gneiss::engine::meta {
struct type_definition {
  engine::guid guid{};
  /** 可空；非空时必须与 guid 一一对应，不持久化。 */
  engine::type_id type_id{};
  std::uint32_t schema_version{};
  std::string_view name{};
  std::span<const property_definition> properties{};
  object_ops object{};
  meta::metadata extensions{};
  /** 模板注册填充的静态能力；运行期定义可留空，不拥有描述或对象。 */
  const detail::type_ops* native{};
};
/** 枚举注册输入；条目和扩展在提交时复制，不拥有实例。 */
struct enum_definition {
  engine::guid guid{};
  engine::type_id type_id{};
  std::uint32_t schema_version{};
  std::string_view name{};
  value_kind kind = value_kind::invalid;
  std::span<const enum_entry> entries{};
  meta::metadata extensions{};
  const detail::type_ops* native{};
};
}
