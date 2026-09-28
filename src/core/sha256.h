// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace gneiss::core {

using sha256_digest = std::array<std::byte, 32U>;

/** 计算内存数据的 SHA-256；无堆分配，用于内容版本校验与离线资产摘要。 */
[[nodiscard]] sha256_digest sha256(std::span<const std::byte> bytes) noexcept;

} // namespace gneiss::core
