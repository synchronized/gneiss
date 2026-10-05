// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/asset/texture_ktx2.hpp"
#include <gneiss/render.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gneiss::asset_internal {
class texture_payload_source;
}

namespace gneiss::render_internal {

struct mesh_resource {
  std::vector<gneiss_mesh_vertex> vertices;
  std::vector<gneiss_mesh_normal> normals;
  std::vector<std::uint32_t> indices;
  std::vector<gneiss_mesh_tangent> tangents{};
  std::vector<gneiss_mesh_uv> uv1{};
  std::vector<gneiss_mesh_color> colors{};
  /** 资产候选与累计驻留使用相同的有效数据字节数，不含容器容量与 GPU 镜像。 */
  [[nodiscard]] std::size_t data_bytes() const noexcept {
    return vertices.size() * sizeof(gneiss_mesh_vertex) +
           normals.size() * sizeof(gneiss_mesh_normal) + indices.size() * sizeof(std::uint32_t) +
           tangents.size() * sizeof(gneiss_mesh_tangent) + uv1.size() * sizeof(gneiss_mesh_uv) +
           colors.size() * sizeof(gneiss_mesh_color);
  }
};

struct material_resource {
  float red;
  float green;
  float blue;
  float alpha;
  gneiss_texture base_color_texture;
  float metallic;
  float roughness;
  gneiss_texture metallic_roughness_texture{};
  gneiss_texture normal_texture{};
  gneiss_texture occlusion_texture{};
  gneiss_texture emissive_texture{};
  float normal_scale{1.0F};
  float occlusion_strength{1.0F};
  std::array<float, 3> emissive{};
  gneiss_material_alpha_mode alpha_mode{GNEISS_MATERIAL_ALPHA_OPAQUE};
  std::uint32_t double_sided{};
  float alpha_cutoff{0.5F};
  std::array<gneiss_texture_sampling, 5> sampling{
      {GNEISS_TEXTURE_SAMPLING_INIT, GNEISS_TEXTURE_SAMPLING_INIT, GNEISS_TEXTURE_SAMPLING_INIT,
       GNEISS_TEXTURE_SAMPLING_INIT, GNEISS_TEXTURE_SAMPLING_INIT}};

  [[nodiscard]] std::array<gneiss_texture, 5> texture_handles() const noexcept {
    return {base_color_texture, metallic_roughness_texture, normal_texture, occlusion_texture,
            emissive_texture};
  }
  [[nodiscard]] std::uint32_t uv1_mask() const noexcept {
    std::uint32_t mask{};
    const auto handles = texture_handles();
    for (std::size_t slot = 0; slot < handles.size(); ++slot)
      if (handles[slot] != GNEISS_NULL_TEXTURE && sampling[slot].uv_set == 1U)
        mask |= (1U << slot);
    return mask;
  }
  void set_texture(std::size_t slot, gneiss_texture texture) noexcept {
    const std::array slots{&base_color_texture, &metallic_roughness_texture, &normal_texture,
                           &occlusion_texture, &emissive_texture};
    *slots[slot] = texture;
  }
  [[nodiscard]] gneiss_material_desc description() const noexcept {
    gneiss_material_desc desc = GNEISS_MATERIAL_DESC_INIT;
    desc.red = red;
    desc.green = green;
    desc.blue = blue;
    desc.alpha = alpha;
    desc.base_color_texture = base_color_texture;
    desc.metallic = metallic;
    desc.roughness = roughness;
    desc.metallic_roughness_texture = metallic_roughness_texture;
    desc.normal_texture = normal_texture;
    desc.occlusion_texture = occlusion_texture;
    desc.emissive_texture = emissive_texture;
    desc.normal_scale = normal_scale;
    desc.occlusion_strength = occlusion_strength;
    desc.alpha_mode = alpha_mode;
    desc.double_sided = double_sided;
    desc.alpha_cutoff = alpha_cutoff;
    for (std::size_t i = 0; i < sampling.size(); ++i)
      desc.sampling[i] = sampling[i];
    for (std::size_t i = 0; i < emissive.size(); ++i)
      desc.emissive[i] = emissive[i];
    return desc;
  }
};

/** 渲染服务初始化后发布的不可变值；位顺序为 RGBA8 linear/sRGB、BC7 linear/sRGB。
 * generation 为零表示未绑定设备，保留整包兼容路径；不携带后端对象。 */
struct texture_prepare_profile {
  std::uint64_t generation{};
  std::array<bool, 4> sampled_transfer_formats{};
  bool operator==(const texture_prepare_profile&) const = default;
};

struct texture_resource {
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t format;
  std::uint32_t color_space;
  std::vector<asset_internal::texture_mip> levels;
  std::vector<std::byte> manifest;
  std::vector<std::byte> payload;
  /** profile 非零时 payload 从选中变体起点开始，不含其他变体。 */
  texture_prepare_profile profile{};
  std::uint32_t selected_variant{UINT32_MAX};
  /** 仅文件选中变体可重建；长期资源不强持有上传负载。 */
  std::shared_ptr<const asset_internal::texture_payload_source> payload_source{};
  std::weak_ptr<const std::vector<std::byte>> upload_payload{};
};

} // namespace gneiss::render_internal
