// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/meta/instance.hpp>
#include "ui/panels/property_path.hpp"
#include <optional>
namespace gneiss::editor {
/** 显式复制对象内容后修改，不改变源引用；结果可用于整项替换。 */
gneiss::result<gneiss::value> edit_reference_snapshot(const gneiss::value&, const property_edit&);
bool draw_property_value(const char* name, gneiss::value&, const gneiss::meta::type&,
                         std::uint64_t& serial, gneiss::error&);
/** source 为同类型来源快照，仅在本次绘制借用；恢复返回独立值，不直接修改对象。
 * 只为可写标量字段提供来源提示与恢复，调用者仍负责校验、历史及异步加载。 */
std::optional<property_edit>
draw_reflected_properties(const gneiss::meta::const_instance&, const char* name,
                          std::uint64_t& serial, gneiss::error&,
                          std::span<const gneiss::property_id> readonly = {},
                          const gneiss::meta::const_instance* source = nullptr);
}
