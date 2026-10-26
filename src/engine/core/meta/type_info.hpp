// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/guid.hpp"
#include "engine/core/meta/field.hpp"
#include "engine/core/result.hpp"
#include "engine/core/value.hpp"
#include <cstdint>
#include <string_view>
#include <vector>

namespace gneiss {
using property_vec3 = engine::vector3;
using property_quaternion = engine::quaternion;
// 缺省成员值允许 designated 初始化省略字段，避免消费者缺省字段告警。
// NOLINTBEGIN(readability-redundant-member-init)
/** 名称由冻结 Registry 借出，有效期不超过 Registry。 */
struct field_info {
  field_id id{};
  gneiss::engine::guid type_guid{};
  field_flags flags = field_flags::none;
  std::string_view name{};
  gneiss::engine::value_kind kind = gneiss::engine::value_kind::invalid;
  property_capabilities capabilities = property_capabilities::none;
};
/** 自有字段数组；名称借用冻结 Registry 的文本，复制/移动不产生临时数组悬空。 */
struct type_info {
  gneiss::engine::guid guid{};
  std::uint32_t schema_version = 0;
  std::string_view name{};
  std::vector<field_info> fields{};
};
// NOLINTEND(readability-redundant-member-init)
} // namespace gneiss
