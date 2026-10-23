// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "asset_parsing.hpp"
#include "engine/resource/types/material_document.hpp"

namespace gneiss::engine::resource {
/** 复用批次已经读取的字节；不进行 I/O、来源复验或共享版本登记。 */
inline gneiss_result decode_material_document(const std::vector<std::byte>& bytes,
                                              material_document& value,
                                              asset_diagnostic& diagnostic) {
  return asset_parsing::parse_material(bytes, value.source, diagnostic);
}
}
