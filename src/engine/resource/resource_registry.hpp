// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/reflection/serialization.hpp"
namespace gneiss::engine {
/** 共享的只读内置资源 Registry，当前登记材质 codec；失败保持输出。 */
[[nodiscard]] GNEISS_API result builtin_resource_registry(registry_ref& output) noexcept;
}
