// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_RESOURCE_SERVICE_H_
#define GNEISS_RENDER_RENDER_RESOURCE_SERVICE_H_

#include "core/rid_table.h"

#include <gneiss/render.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gneiss::render_internal {

struct mesh_resource {
  std::vector<gneiss_mesh_vertex> vertices;
};

struct material_resource {
  float red;
  float green;
  float blue;
  float alpha;
};

class render_resource_service final {
public:
  render_resource_service() noexcept;

  [[nodiscard]] bool is_valid() const noexcept { return domain_ != 0U; }
  [[nodiscard]] gneiss_result create_mesh(const gneiss_mesh_desc& desc,
                                          gneiss_mesh* out_mesh) noexcept;
  [[nodiscard]] gneiss_result destroy_mesh(gneiss_mesh mesh) noexcept;
  [[nodiscard]] gneiss_result create_material(const gneiss_material_desc& desc,
                                              gneiss_material* out_material) noexcept;
  [[nodiscard]] gneiss_result destroy_material(gneiss_material material) noexcept;
  [[nodiscard]] const mesh_resource* get_mesh(gneiss_mesh mesh) const noexcept;
  [[nodiscard]] const material_resource* get_material(gneiss_material material) const noexcept;
  [[nodiscard]] std::size_t live_resource_count() const noexcept;

private:
  std::uint16_t domain_{};
  core::rid_table<mesh_resource> meshes_;
  core::rid_table<material_resource> materials_;
};

} // namespace gneiss::render_internal

#endif
