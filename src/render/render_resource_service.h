// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_RESOURCE_SERVICE_H_
#define GNEISS_RENDER_RENDER_RESOURCE_SERVICE_H_

#include "asset/texture_ktx2.h"
#include "core/rid_table.h"

#include <gneiss/render.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gneiss::render_internal {

struct mesh_resource {
  std::vector<gneiss_mesh_vertex> vertices;
  std::vector<gneiss_mesh_normal> normals;
  std::vector<std::uint32_t> indices;
};

struct material_resource {
  float red;
  float green;
  float blue;
  float alpha;
  gneiss_texture base_color_texture;
  float metallic;
  float roughness;
};

struct texture_resource {
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t format;
  std::uint32_t color_space;
  std::vector<asset_internal::texture_mip> levels;
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
  [[nodiscard]] gneiss_result create_texture(const gneiss_texture_desc& desc,
                                             gneiss_texture* out_texture) noexcept;
  /** 创建已经过容器校验的内部多 Mip Texture。 */
  [[nodiscard]] gneiss_result create_texture(texture_resource resource,
                                             gneiss_texture* out_texture) noexcept;
  [[nodiscard]] gneiss_result destroy_texture(gneiss_texture texture) noexcept;
  [[nodiscard]] const mesh_resource* get_mesh(gneiss_mesh mesh) const noexcept;
  [[nodiscard]] const material_resource* get_material(gneiss_material material) const noexcept;
  [[nodiscard]] const texture_resource* get_texture(gneiss_texture texture) const noexcept;
  [[nodiscard]] std::shared_ptr<const mesh_resource> share_mesh(gneiss_mesh mesh) const noexcept;
  [[nodiscard]] std::shared_ptr<const material_resource>
  share_material(gneiss_material material) const noexcept;
  [[nodiscard]] std::shared_ptr<const texture_resource>
  share_texture(gneiss_texture texture) const noexcept;
  [[nodiscard]] std::size_t live_resource_count() const noexcept;

private:
  std::uint16_t domain_{};
  core::rid_table<std::shared_ptr<const mesh_resource>> meshes_;
  core::rid_table<std::shared_ptr<const material_resource>> materials_;
  core::rid_table<std::shared_ptr<const texture_resource>> textures_;
};

} // namespace gneiss::render_internal

#endif
