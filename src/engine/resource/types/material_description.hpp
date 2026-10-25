// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/type_guid.hpp"
#include "material_parameters.hpp"
#include <array>
#include <string>
#include <string_view>

namespace gneiss::engine {
/** 纯 CPU 材质描述；纹理依赖使用 URI，不包含资源句柄。 */
struct material_description final {
  inline static constexpr std::string_view type_name = "material_description";
  inline static constexpr engine::type_guid type_guid{
      engine::guid::from_bytes({0xcc, 0x6d, 0xf6, 0x31, 0x27, 0x27, 0x40, 0xdc, 0xa3, 0xad, 0xfc,
                                0xe9, 0xa5, 0x7d, 0x49, 0x09})};
  material_parameters parameters;
  std::array<std::string, 5> texture_uris;
};
}
