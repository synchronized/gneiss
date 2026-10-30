// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "panels/property_path.hpp"
#include <optional>
namespace gneiss::editor {
/** 控件输出拥有数据的操作；消费前必须重新解析路径，不保留成员地址。 */
struct property_edit {
  property_path path;
  engine::value data{};
  engine::type_id object_type{};
  std::shared_ptr<void> object{};
};
bool draw_property_value(const char* name, engine::value&,
                         std::span<const engine::meta::enum_entry>, std::uint64_t& serial,
                         engine::error&);
std::optional<property_edit> draw_reflected_properties(const engine::meta::instance&,
                                                       const char* name, std::uint64_t& serial,
                                                       engine::error&);
}
