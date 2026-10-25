// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/types/texture_types.hpp"
#include <string>

namespace gneiss::engine {
/** 文件描述的运行时视图；尺寸和 mip 布局由负载容器负责，不重复存储。 */
struct texture_description final {
  std::string uri;
  texture_color_space color_space{};
};
} // namespace gneiss::engine
