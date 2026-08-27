// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstddef>

namespace gneiss::tooling::asset_import {

struct import_ir_summary {
  std::size_t scene_count{};
  std::size_t node_count{};
  std::size_t mesh_count{};
  std::size_t primitive_count{};
  std::size_t material_count{};
  std::size_t image_count{};
};

} // namespace gneiss::tooling::asset_import
