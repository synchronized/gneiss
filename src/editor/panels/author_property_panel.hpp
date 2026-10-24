// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/result.hpp"
#include "panels/property_inspector_model.hpp"

#include <span>

namespace gneiss::editor {

/** 主线程单次绘制借用；回调不得保留组件或属性引用，也不得重建当前属性数组。
 * write 必须提供，编辑成功与失败均由返回值报告。 */
struct author_property_actions final {
  void* context = nullptr;
  gneiss::engine::core::result (*write)(void*, const inspector_component&,
                                        const inspector_property&,
                                        const engine::core::reflection::property_value&,
                                        std::uint64_t) = nullptr;
};

void draw_author_properties(std::span<const inspector_component> components,
                            std::uint64_t& edit_serial, gneiss::engine::core::result& error,
                            const author_property_actions& actions);

} // namespace gneiss::editor
