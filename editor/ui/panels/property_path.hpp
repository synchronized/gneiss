// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/meta/property_path.hpp>
#include <gneiss/engine/core/types/ref.hpp>
#include <variant>
namespace gneiss::editor {
using gneiss::meta::property_path;
using gneiss::meta::property_step;
/** 控件输出拥有数据的操作；消费前必须重新解析路径，不保留成员地址。 */
struct property_edit {
  property_path path;
  gneiss::value data{};
  gneiss::type_id object_type{};
  gneiss::ref<void> object{};
};
}
