// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <cstdint>

namespace gneiss::engine::core {
/** 内部资源标识，不持有资源；零值无效。类型、代次与所属服务由资源表校验。
 * 保持现有 64 位编码；此别名不提供编译期资源种类隔离。 */
using rid = std::uint64_t;
inline constexpr rid null_rid = 0;
} // namespace gneiss::engine::core
