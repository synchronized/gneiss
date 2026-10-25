// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "material_parameters.hpp"
#include <array>
#include <string>

namespace gneiss::engine {
/** 纯 CPU 材质描述；纹理依赖使用 URI，不包含资源句柄。 */
struct material_description final {
  material_parameters parameters;
  std::array<std::string, 5> texture_uris;
};
}
