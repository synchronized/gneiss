// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/property.hpp"
#include "engine/core/meta/serialization_types.hpp"
#include "engine/core/type_id.hpp"
#include <span>
#include <vector>

namespace gneiss::engine::meta {
struct type_definition {
  engine::guid guid{};
  /** 可空；非空时必须与 guid 一一对应，不持久化。 */
  engine::type_id type_id{};
  std::uint32_t schema_version{};
  std::string_view name{};
  std::span<const property_definition> properties{};
  serialization_binding serialization{};
};
/** 自有字段数组；名称借用冻结 Registry 的文本，复制/移动不产生临时数组悬空。 */
struct type_info {
  gneiss::engine::guid guid{};
  std::uint32_t schema_version = 0;
  std::string_view name{};
  std::vector<property_info> properties{};
};
} // namespace gneiss::engine::meta
