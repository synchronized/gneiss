// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace gneiss::tooling::asset_build {

/** 离线检查封装、Manifest 和全部变体负载摘要；成功时输出变体及 Mip 布局。 */
[[nodiscard]] bool inspect_runtime_texture(std::span<const std::byte> bytes, std::string& summary,
                                           std::string& diagnostic) noexcept;

} // namespace gneiss::tooling::asset_build
