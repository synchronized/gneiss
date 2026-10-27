// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <cstdint>

namespace gneiss::engine {
/** 材质纯值语义；资产与渲染共用，不持有资源或后端对象。 */
enum class material_alpha_mode : std::uint32_t { opaque, mask, blend };
} // namespace gneiss::engine
