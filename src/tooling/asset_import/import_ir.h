// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gneiss::tooling::asset_import {

struct import_ir_summary {
  std::size_t scene_count{};
  std::size_t node_count{};
  std::size_t mesh_count{};
  std::size_t primitive_count{};
  std::size_t material_count{};
  std::size_t image_count{};
};

struct import_ir_node {
  std::string name;
  std::optional<std::size_t> mesh_index;
  std::vector<std::size_t> children;
};

struct import_ir_primitive {
  std::size_t position_accessor{};
  std::size_t normal_accessor{};
  std::size_t texcoord_accessor{};
  std::optional<std::size_t> index_accessor;
  std::optional<std::size_t> material_index;
  struct vertex {
    float position[3]{};
    float normal[3]{};
    float texcoord[2]{};
  };
  std::vector<vertex> vertices;
  std::vector<std::uint32_t> indices;
};

struct import_ir_mesh {
  std::string name;
  std::vector<import_ir_primitive> primitives;
};

struct import_ir_material {
  std::string name;
};

struct import_ir {
  std::vector<import_ir_node> nodes;
  std::vector<import_ir_mesh> meshes;
  std::vector<import_ir_material> materials;
};

} // namespace gneiss::tooling::asset_import
