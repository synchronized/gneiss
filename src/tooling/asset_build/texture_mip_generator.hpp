// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/resource/texture/texture_ktx2.hpp"

namespace gneiss::tooling {

/** 面积加权生成下一层 RGBA8；输入布局无效或 normal 与 sRGB 冲突时返回空数组。 */
[[nodiscard]] std::vector<std::byte> generate_texture_mip(const gneiss::engine::texture_mip& source,
                                                          gneiss::engine::texture_transfer transfer,
                                                          bool normal_map);

} // namespace gneiss::tooling
