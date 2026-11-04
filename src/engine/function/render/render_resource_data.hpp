// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/rid.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/mesh/mesh_data.hpp"
#include "engine/resource/texture/texture_data.hpp"
#include "engine/resource/texture/texture_description.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gneiss::engine {

struct material_data {
  material_parameters parameters{};
  rid base_color_texture{};
  rid metallic_roughness_texture{};
  rid normal_texture{};
  rid occlusion_texture{};
  rid emissive_texture{};

  [[nodiscard]] std::array<rid, 5> texture_handles() const noexcept {
    return {base_color_texture, metallic_roughness_texture, normal_texture, occlusion_texture,
            emissive_texture};
  }
  [[nodiscard]] std::uint32_t uv1_mask() const noexcept {
    std::uint32_t mask{};
    const auto handles = texture_handles();
    for (std::size_t slot = 0; slot < handles.size(); ++slot)
      if (handles[slot] != null_rid && parameters.sampling[slot].uv_set == 1U)
        mask |= (1U << slot);
    return mask;
  }
  void set_texture(std::size_t slot, rid texture) noexcept {
    const std::array slots{&base_color_texture, &metallic_roughness_texture, &normal_texture,
                           &occlusion_texture, &emissive_texture};
    *slots[slot] = texture;
  }
};

} // namespace gneiss::engine
