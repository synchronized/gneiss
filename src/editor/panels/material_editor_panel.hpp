// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "panels/material_editor_model.hpp"
namespace gneiss::editor {
/** 单次绘制借用；反射遍历结束后才提交变更，不跨帧缓存成员地址。 */
void draw_material_editor(material_editor_model&, engine::files::service&, bool& visible,
                          std::uint64_t& edit_serial, engine::error& status, bool& focused);
}
