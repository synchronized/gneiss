// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace gneiss::tooling::asset_build {

using sha256_digest = std::array<std::byte, 32U>;

/** 计算内存数据的 SHA-256；该实现仅供离线资产工具生成稳定标识与负载摘要。 */
[[nodiscard]] sha256_digest sha256(std::span<const std::byte> bytes) noexcept;

} // namespace gneiss::tooling::asset_build
