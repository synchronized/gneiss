// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/sha256.hpp"
#include "material_source.hpp"
#include <caors/resources/resource.h>

namespace gneiss::engine::resource {
/** CPU 文档，不持有纹理或 RID；发布后由宿主按不可变对象使用。 */
struct material_document final : ::caors::resource {
  using ::caors::resource::resource;
  material_source source;
  engine::core::sha256_digest digest{};
  std::uint64_t source_bytes{};
};
}
