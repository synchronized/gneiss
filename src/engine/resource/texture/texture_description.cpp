// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "texture_description.hpp"
#include <cmath>
#include <limits>
namespace gneiss::engine {
namespace {
serialization::diagnostic invalid_description(std::string path, std::string message) {
  return {.path = std::move(path), .message = std::move(message), .error = error::invalid_argument};
}
}
serialization::diagnostic validate_texture_description(const texture_description& v) noexcept try {
  if (v.uri.empty())
    return invalid_description("/source", "纹理 source 必须是非空 URI");
  if (v.color_space != texture_color_space::linear && v.color_space != texture_color_space::srgb)
    return invalid_description("/color_space", "未知颜色空间");
  if (v.usage > texture_usage::normal)
    return invalid_description("/usage", "未知纹理用途");
  if (v.usage == texture_usage::normal && v.color_space != texture_color_space::linear)
    return invalid_description("/usage", "法线纹理必须使用 linear");
  return {};
} catch (const std::bad_alloc&) {
  return {.error = error::out_of_memory};
} catch (...) {
  return {.error = error::internal};
}
}
