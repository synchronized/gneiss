// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/math/types.hpp"
#include "engine/resource/material/material_types.hpp"
#include "engine/resource/texture/texture_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gneiss::tooling {

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
  gneiss::engine::math::vector3 translation{};
  gneiss::engine::math::quaternion rotation{1.0F, 0.0F, 0.0F, 0.0F};
  gneiss::engine::math::vector3 scale{1.0F};
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
    std::array<float, 4> tangent{1.0F, 0.0F, 0.0F, 1.0F};
    std::array<float, 2> uv1{};
    std::array<float, 4> color{1, 1, 1, 1};
  };
  std::vector<vertex> vertices;
  std::vector<std::uint32_t> indices;
  bool has_uv1{};
  bool has_color{};
  std::uint32_t tangent_uv_set{};
  bool repaired_tangents{};
};

struct import_ir_mesh {
  std::string name;
  std::vector<import_ir_primitive> primitives;
};

struct import_ir_material {
  std::string name;
  std::array<float, 4> base_color{1.0F, 1.0F, 1.0F, 1.0F};
  std::optional<std::size_t> base_color_image_index;
  float metallic{};
  float roughness{1.0F};
  std::optional<std::size_t> metallic_roughness_image_index;
  std::optional<std::size_t> normal_image_index;
  std::optional<std::size_t> occlusion_image_index;
  std::optional<std::size_t> emissive_image_index;
  float normal_scale{1.0F};
  float occlusion_strength{1.0F};
  std::array<float, 3> emissive{};
  gneiss::engine::material_alpha_mode alpha_mode{gneiss::engine::material_alpha_mode::opaque};
  std::uint32_t double_sided{};
  float alpha_cutoff{0.5F};
  std::array<gneiss::engine::texture_sampling, 5> sampling{};

  [[nodiscard]] std::array<std::optional<std::size_t>, 5> texture_indices() const {
    return {base_color_image_index, metallic_roughness_image_index, normal_image_index,
            occlusion_image_index, emissive_image_index};
  }
};

struct import_ir_image {
  std::string name;
  bool is_png{};
  std::vector<std::byte> bytes;
};

struct import_ir {
  std::vector<import_ir_node> nodes;
  std::vector<import_ir_mesh> meshes;
  std::vector<import_ir_material> materials;
  std::vector<import_ir_image> images;
};

/** 颜色变体保留原输出名；线性数据与法线派生源使用独立路径和处理语义。 */
[[nodiscard]] inline std::array<bool, 3> image_variants(const import_ir& data, std::size_t image) {
  std::array<bool, 3> uses{};
  for (const auto& material : data.materials) {
    uses[0] = uses[0] || material.base_color_image_index == image ||
              material.emissive_image_index == image;
    uses[1] = uses[1] || material.metallic_roughness_image_index == image ||
              material.occlusion_image_index == image;
    uses[2] = uses[2] || material.normal_image_index == image;
  }
  // 未引用图片仍按原规则保留；已引用的图片只生成实际用途，避免重复 Cook 大纹理。
  if (!uses[0] && !uses[1] && !uses[2]) {
    uses[0] = true;
  }
  return uses;
}

inline constexpr std::array image_variant_suffixes{"", "-linear", "-normal"};

} // namespace gneiss::tooling
