// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/rid.hpp"
#include "engine/resource/types/material_types.hpp"
#include "engine/resource/types/mesh_data.hpp"
#include "engine/resource/types/texture_preparation_data.hpp"
#include "engine/resource/types/texture_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gneiss::engine::resource {
class texture_payload_source;
}

namespace gneiss::engine::render {

using mesh_resource = engine::resource::mesh_data;

struct material_resource {
  float red{1.0F};
  float green{1.0F};
  float blue{1.0F};
  float alpha{1.0F};
  engine::core::rid base_color_texture{};
  float metallic{0.0F};
  float roughness{1.0F};
  engine::core::rid metallic_roughness_texture{};
  engine::core::rid normal_texture{};
  engine::core::rid occlusion_texture{};
  engine::core::rid emissive_texture{};
  float normal_scale{1.0F};
  float occlusion_strength{1.0F};
  std::array<float, 3> emissive{};
  engine::resource::material_alpha_mode alpha_mode{engine::resource::material_alpha_mode::opaque};
  bool double_sided{};
  float alpha_cutoff{0.5F};
  std::array<engine::resource::texture_sampling, 5> sampling{};

  [[nodiscard]] std::array<engine::core::rid, 5> texture_handles() const noexcept {
    return {base_color_texture, metallic_roughness_texture, normal_texture, occlusion_texture,
            emissive_texture};
  }
  [[nodiscard]] std::uint32_t uv1_mask() const noexcept {
    std::uint32_t mask{};
    const auto handles = texture_handles();
    for (std::size_t slot = 0; slot < handles.size(); ++slot)
      if (handles[slot] != engine::core::null_rid && sampling[slot].uv_set == 1U)
        mask |= (1U << slot);
    return mask;
  }
  void set_texture(std::size_t slot, engine::core::rid texture) noexcept {
    const std::array slots{&base_color_texture, &metallic_roughness_texture, &normal_texture,
                           &occlusion_texture, &emissive_texture};
    *slots[slot] = texture;
  }
};

using texture_prepare_profile = engine::resource::texture_prepare_profile;

struct texture_resource {
  std::uint32_t width;
  std::uint32_t height;
  engine::resource::texture_format format;
  engine::resource::texture_color_space color_space;
  std::vector<engine::resource::texture_mip> levels;
  std::vector<std::byte> manifest;
  std::vector<std::byte> payload;
  /** profile 非零时 payload 从选中变体起点开始，不含其他变体。 */
  texture_prepare_profile profile{};
  std::uint32_t selected_variant{UINT32_MAX};
  /** 仅文件选中变体可重建；长期资源不强持有上传负载。 */
  std::shared_ptr<const engine::resource::texture_payload_source> payload_source{};
  std::weak_ptr<const std::vector<std::byte>> upload_payload{};
};

} // namespace gneiss::engine::render
