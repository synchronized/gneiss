// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/core/types/value.hpp>

#include <string_view>

namespace gneiss::meta {
enum class type_kind {
  invalid,
  empty,
  scalar,
  object,
  sequence,
  associative,
  optional,
  reference,
  variant
};
/** 枚举显示项。注册输入借用至调用结束；查询名称借用字符串池至 Engine 卸载。
 * 查询名称为完整的 NUL 结尾文本，空名称可无 data；查询项集合仍借用注册表。
 * 不改变类型的业务校验规则。 */
struct enum_entry {
  std::string_view name;
  value data;
  bool operator==(const enum_entry&) const = default;
};
}
