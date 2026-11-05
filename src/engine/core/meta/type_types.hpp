// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/value.hpp"
#include <string>
namespace gneiss::engine::meta {
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
/** 自有枚举显示项，注册时复制；不改变类型的业务校验规则。 */
struct enum_entry {
  std::string name;
  value data;
  bool operator==(const enum_entry&) const = default;
};
}
