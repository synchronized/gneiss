// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <cstdint>

namespace gneiss::engine {
/** 逻辑像素描述；压缩容器的实际 GPU 格式仍由所选变体描述。 */
enum class texture_format : std::uint32_t { rgba8_unorm = 1 };
enum class texture_color_space : std::uint32_t { linear = 1, srgb = 2 };
} // namespace gneiss::engine
