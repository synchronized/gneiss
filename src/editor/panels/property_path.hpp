// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/property.hpp"
#include <memory>
#include <variant>
namespace gneiss::editor {
using property_step = std::variant<engine::meta::property_id, std::size_t>;
using property_path = std::vector<property_step>;
/** 控件输出拥有数据的操作；消费前必须重新解析路径，不保留成员地址。 */
struct property_edit {
  property_path path;
  engine::value data{};
  engine::type_id object_type{};
  std::shared_ptr<void> object{};
};
}
