// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_RESOURCE_SERVICE_H_
#define GNEISS_RENDER_RENDER_RESOURCE_SERVICE_H_

#include "asset/texture_ktx2.h"
#include "core/rid_table.h"

#include <gneiss/render.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

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
};

class render_resource_service final {
public:
  render_resource_service() noexcept;

  [[nodiscard]] bool is_valid() const noexcept { return domain_ != 0U; }
  [[nodiscard]] gneiss_result create_mesh(const gneiss_mesh_desc& desc,
                                          gneiss_mesh* out_mesh) noexcept;
  /** 内部 CPU 候选已经校验，移动所有权避免主线程复制大数组。 */
  [[nodiscard]] gneiss_result create_prepared_mesh(mesh_resource resource,
                                                   gneiss_mesh* output) noexcept;
  [[nodiscard]] bool replace_mesh(gneiss_mesh rid,
                                  std::shared_ptr<const mesh_resource> data) noexcept;
  [[nodiscard]] bool replace_material(gneiss_material rid,
                                      std::shared_ptr<const material_resource> data) noexcept;
  [[nodiscard]] gneiss_result destroy_mesh(gneiss_mesh mesh) noexcept;
  [[nodiscard]] gneiss_result create_material(const gneiss_material_desc& desc,
                                              gneiss_material* out_material) noexcept;
  [[nodiscard]] gneiss_result destroy_material(gneiss_material material) noexcept;
  [[nodiscard]] gneiss_result create_texture(const gneiss_texture_desc& desc,
                                             gneiss_texture* out_texture) noexcept;
  /** 创建已经过容器校验的内部多 Mip Texture。 */
  [[nodiscard]] gneiss_result create_texture(texture_resource resource,
                                             gneiss_texture* out_texture) noexcept;
  /** 保存已经过外层容器与 Granit Manifest 检查的运行纹理。 */
  [[nodiscard]] gneiss_result create_packaged_texture(texture_resource resource,
                                                      gneiss_texture* out_texture) noexcept;
  [[nodiscard]] gneiss_result destroy_texture(gneiss_texture texture) noexcept;
  /** 主线程提交已验证候选；保持 RID generation，既有帧仍拥有旧数据快照。 */
  [[nodiscard]] bool replace_texture(gneiss_texture texture,
                                     std::shared_ptr<const texture_resource> prepared) noexcept;
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
