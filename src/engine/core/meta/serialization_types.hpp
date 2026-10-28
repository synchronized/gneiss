// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/error.hpp"
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace gneiss::engine::meta {
/** 编解码错误位置；拥有文本，不依赖资源、文件系统或后端。 */
struct serialization_diagnostic {
  std::size_t byte_offset{};
  std::string path;
  std::string message;
};
/** 每种类型可选一组编解码函数，文件格式及版本由 codec 识别。对象及字节仅在调用期间借用。
 * 回调必须可并发操作不同对象；模块需在 Registry 使用期间存活。 */
struct serialization_binding {
  error (*encode)(const void*, std::vector<std::byte>&, serialization_diagnostic&){};
  error (*decode)(std::span<const std::byte>, void*, serialization_diagnostic&){};
};
} // namespace gneiss::engine::meta
