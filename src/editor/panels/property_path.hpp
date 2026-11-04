// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/property_path.hpp"
#include "engine/core/ref.hpp"
#include <variant>
namespace gneiss::editor {
using engine::meta::property_path;
using engine::meta::property_step;
/** 控件输出拥有数据的操作；消费前必须重新解析路径，不保留成员地址。 */
struct property_edit {
  property_path path;
  engine::value data{};
  engine::type_id object_type{};
  engine::ref<void> object{};
};
}
