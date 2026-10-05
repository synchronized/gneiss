// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_RESOURCE_SERVICE_HPP_
#define GNEISS_RENDER_RENDER_RESOURCE_SERVICE_HPP_

#include "engine/core/rid_table.hpp"
#include "engine/function/render/render_resource_data.hpp"

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

struct render_memory_usage {
  std::uint64_t logical_bytes{};
  std::uint64_t cpu_data_bytes{};
};

class render_resource_service final {
public:
  static constexpr std::uint64_t default_memory_limit = UINT64_C(4) * 1024U * 1024U * 1024U;
  explicit render_resource_service(std::uint64_t memory_limit = default_memory_limit) noexcept;
  /** 所属线程查询；跟踪共享对象，包含仍被旧帧持有的已销毁 RID 数据，不强持有对象。 */
  [[nodiscard]] render_memory_usage memory_usage() const noexcept;
  [[nodiscard]] std::uint64_t available_memory_bytes() const noexcept;

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
  [[nodiscard]] gneiss_result create_material(const material_resource& value,
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
  template <typename Resource>
  std::shared_ptr<const Resource> track(std::shared_ptr<const Resource> resource,
                                        std::vector<std::weak_ptr<const Resource>>& history);
  std::uint64_t memory_limit_{};
  mutable std::vector<std::weak_ptr<const mesh_resource>> mesh_history_;
  mutable std::vector<std::weak_ptr<const material_resource>> material_history_;
  mutable std::vector<std::weak_ptr<const texture_resource>> texture_history_;
  std::uint16_t domain_{};
  core::rid_table<std::shared_ptr<const mesh_resource>> meshes_;
  core::rid_table<std::shared_ptr<const material_resource>> materials_;
  core::rid_table<std::shared_ptr<const texture_resource>> textures_;
};

} // namespace gneiss::render_internal

#endif
