// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "panels/property_path.hpp"
#include <optional>
namespace gneiss::editor {
/** 显式复制对象内容后修改，不改变源引用；结果可用于整项替换。 */
engine::result<engine::value> edit_reference_snapshot(const engine::value&,
                                                      const property_edit&) noexcept;
bool draw_property_value(const char* name, engine::value&, const engine::meta::type&,
                         std::uint64_t& serial, engine::error&);
/** source 为同类型来源快照，仅在本次绘制借用；恢复返回独立值，不直接修改对象。
 * 只为可写标量字段提供来源提示与恢复，调用者仍负责校验、历史及异步加载。 */
std::optional<property_edit>
draw_reflected_properties(const engine::meta::const_instance&, const char* name,
                          std::uint64_t& serial, engine::error&,
                          std::span<const engine::meta::property_id> readonly = {},
                          const engine::meta::const_instance* source = nullptr);
}
