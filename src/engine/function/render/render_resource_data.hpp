// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/rid.hpp"
#include "engine/resource/material/material_parameters.hpp"
#include "engine/resource/mesh/mesh_data.hpp"
#include "engine/resource/texture/texture_data.hpp"
#include "engine/resource/texture/texture_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gneiss::engine {

struct material_data {
  material_parameters parameters{};
  gneiss::engine::rid base_color_texture{};
  gneiss::engine::rid metallic_roughness_texture{};
  gneiss::engine::rid normal_texture{};
  gneiss::engine::rid occlusion_texture{};
  gneiss::engine::rid emissive_texture{};

  [[nodiscard]] std::array<gneiss::engine::rid, 5> texture_handles() const noexcept {
    return {base_color_texture, metallic_roughness_texture, normal_texture, occlusion_texture,
            emissive_texture};
  }
  [[nodiscard]] std::uint32_t uv1_mask() const noexcept {
    std::uint32_t mask{};
    const auto handles = texture_handles();
    for (std::size_t slot = 0; slot < handles.size(); ++slot)
      if (handles[slot] != gneiss::engine::null_rid && parameters.sampling[slot].uv_set == 1U)
        mask |= (1U << slot);
    return mask;
  }
  void set_texture(std::size_t slot, gneiss::engine::rid texture) noexcept {
    const std::array slots{&base_color_texture, &metallic_roughness_texture, &normal_texture,
                           &occlusion_texture, &emissive_texture};
    *slots[slot] = texture;
  }
};

struct texture_data {
  std::uint32_t width;
  std::uint32_t height;
  gneiss::engine::texture_format format;
  gneiss::engine::texture_color_space color_space;
  std::vector<gneiss::engine::texture_mip> levels;
  std::vector<std::byte> manifest;
  std::vector<std::byte> payload;
  /** profile 非零时 payload 从选中变体起点开始，不含其他变体。 */
  texture_prepare_profile profile{};
  std::uint32_t selected_variant{UINT32_MAX};
};

} // namespace gneiss::engine
