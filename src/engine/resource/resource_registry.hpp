// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/reflection/serialization.hpp"
namespace gneiss::engine {
/** 共享的只读内置资源 Registry，当前登记材质 codec；失败保持输出。 */
[[nodiscard]] GNEISS_API result builtin_resource_registry(registry_ref& output) noexcept;
/** 特殊准备路径复用内置 codec，不引入 CPU 缓存或 GPU 操作。 */
template <class T>
[[nodiscard]] result decode_resource(std::span<const std::byte> bytes, T& output,
                                     serialization_diagnostic& diagnostic) noexcept {
  diagnostic = {};
  registry_ref registry;
  const auto status = builtin_resource_registry(registry);
  return status == result::success ? deserialize(registry, bytes, output, diagnostic) : status;
}
}
