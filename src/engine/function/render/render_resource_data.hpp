// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/resource/types/material_types.hpp"
#include "engine/resource/types/mesh_data.hpp"
#include "engine/resource/types/texture_preparation_data.hpp"
#include "engine/resource/types/texture_types.hpp"
#include "engine/core/rid.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gneiss::asset_internal {
class texture_payload_source;
}

namespace gneiss::render_internal {

using mesh_resource = asset_internal::mesh_data;

struct material_resource {
  float red{1.0F};
  float green{1.0F};
  float blue{1.0F};
  float alpha{1.0F};
  core::rid base_color_texture{};
  float metallic{0.0F};
  float roughness{1.0F};
  core::rid metallic_roughness_texture{};
  core::rid normal_texture{};
  core::rid occlusion_texture{};
  core::rid emissive_texture{};
  float normal_scale{1.0F};
  float occlusion_strength{1.0F};
  std::array<float, 3> emissive{};
  asset_internal::material_alpha_mode alpha_mode{asset_internal::material_alpha_mode::opaque};
  bool double_sided{};
  float alpha_cutoff{0.5F};
  std::array<asset_internal::texture_sampling, 5> sampling{};

  [[nodiscard]] std::array<core::rid, 5> texture_handles() const noexcept {
    return {base_color_texture, metallic_roughness_texture, normal_texture, occlusion_texture,
            emissive_texture};
  }
  [[nodiscard]] std::uint32_t uv1_mask() const noexcept {
    std::uint32_t mask{};
    const auto handles = texture_handles();
    for (std::size_t slot = 0; slot < handles.size(); ++slot)
      if (handles[slot] != core::null_rid && sampling[slot].uv_set == 1U)
        mask |= (1U << slot);
    return mask;
  }
  void set_texture(std::size_t slot, core::rid texture) noexcept {
    const std::array slots{&base_color_texture, &metallic_roughness_texture, &normal_texture,
                           &occlusion_texture, &emissive_texture};
    *slots[slot] = texture;
  }
};

using texture_prepare_profile = asset_internal::texture_prepare_profile;

struct texture_resource {
  std::uint32_t width;
  std::uint32_t height;
  asset_internal::texture_format format;
  asset_internal::texture_color_space color_space;
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
