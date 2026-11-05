// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/error.hpp"
#include <cstddef>
#include <string>

namespace gneiss::engine::serialization {
/** 编解码错误码与位置；拥有文本，不依赖资源、文件系统或后端。 */
struct diagnostic {
  std::size_t byte_offset{};
  std::string path{};
  std::string message{};
  engine::error error{engine::error::success};
};
}
