// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "asset_parsing.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/types/material_description.hpp"

namespace gneiss::engine {
/** 复用批次已经读取的字节；不进行 I/O、来源复验或共享版本登记。 */
inline gneiss::engine::result decode_material_description(const std::vector<std::byte>& bytes,
                                                          material_description& value,
                                                          asset_diagnostic& diagnostic) {
  return gneiss::engine::detail::parse_material(bytes, value, diagnostic);
}
}
