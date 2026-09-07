// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "asset/texture_ktx2.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gneiss::tooling::asset_build {

enum class runtime_texture_build_result : std::uint8_t {
  success,
  invalid_argument,
  encode_failed,
  out_of_memory,
};

/** 将二维 RGBA8 完整 Mip 链构建为含 BC7 优选与 RGBA8 回退变体的运行纹理。 */
[[nodiscard]] runtime_texture_build_result
build_runtime_texture(const asset_internal::texture_ktx2& source, std::vector<std::byte>& output,
                      std::string& diagnostic) noexcept;

} // namespace gneiss::tooling::asset_build
