// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_RESOURCE_SERVICE_HPP_
#define GNEISS_RENDER_RENDER_RESOURCE_SERVICE_HPP_

#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/rid_table.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/function/render/texture_view.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <gneiss/engine/core/export.h>
#include <memory>
#include <vector>

namespace gneiss::engine {
class texture_payload_source;
}

namespace gneiss::engine {

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
  /** 所属线程预留合法 RID；尚无 CPU 内容，不可直接绘制。由 replace_* 填充，destroy_* 取消。
   * 不创建后端对象；所有权属于调用方，销毁和跨服务校验与普通 RID 一致。 */
  [[nodiscard]] GNEISS_API gneiss::engine::result
  reserve_mesh(gneiss::engine::rid* output) noexcept;
  [[nodiscard]] GNEISS_API gneiss::engine::result
  reserve_texture(gneiss::engine::rid* output) noexcept;
  [[nodiscard]] gneiss::engine::result reserve_material(gneiss::engine::rid* output) noexcept;
  /** 只查询 CPU 内容：SUCCESS 已填充，NOT_READY 仅预留，INVALID_HANDLE 已失效或不属于本服务。 */
  [[nodiscard]] gneiss::engine::result mesh_status(gneiss::engine::rid mesh) const noexcept;
  [[nodiscard]] gneiss::engine::result texture_status(gneiss::engine::rid texture) const noexcept;
  [[nodiscard]] gneiss::engine::result material_status(gneiss::engine::rid material) const noexcept;
  [[nodiscard]] gneiss::engine::result create_mesh(const gneiss::engine::mesh_view& desc,
                                                   gneiss::engine::rid* out_mesh) noexcept;
  /** 校验并接收拥有型网格，成功路径不再复制数组。 */
  [[nodiscard]] gneiss::engine::result create_mesh(mesh_resource resource,
                                                   gneiss::engine::rid* output) noexcept;
  /** 内部 CPU 候选已经校验，移动所有权避免主线程复制大数组。 */
  [[nodiscard]] gneiss::engine::result create_prepared_mesh(mesh_resource resource,
                                                            gneiss::engine::rid* output) noexcept;
  /** 已校验不可变 CPU 网格；RID 与旧帧共享同一对象，销毁 RID 不使外部引用失效。 */
  [[nodiscard]] gneiss::engine::result
  create_prepared_mesh(std::shared_ptr<const mesh_resource> resource,
                       gneiss::engine::rid* output) noexcept;
  [[nodiscard]] GNEISS_API bool replace_mesh(gneiss::engine::rid rid,
                                             std::shared_ptr<const mesh_resource> data) noexcept;
  [[nodiscard]] bool replace_material(gneiss::engine::rid rid,
                                      std::shared_ptr<const material_resource> data) noexcept;
  /** 校验并登记临时 CPU 数据，不分配 RID；任务和帧引用共同决定数据寿命。 */
  [[nodiscard]] gneiss::engine::result
  prepare_mesh_data(std::shared_ptr<const mesh_resource> resource,
                    std::shared_ptr<const mesh_resource>& output) noexcept;
  [[nodiscard]] gneiss::engine::result
  prepare_material_data(const material_resource& resource,
                        std::shared_ptr<const material_resource>& output) noexcept;
  [[nodiscard]] gneiss::engine::result
  prepare_texture_data(texture_resource resource,
                       std::shared_ptr<const texture_resource>& output) noexcept;
  [[nodiscard]] gneiss::engine::result destroy_mesh(gneiss::engine::rid mesh) noexcept;
  [[nodiscard]] gneiss::engine::result create_material(const material_resource& value,
                                                       gneiss::engine::rid* out_material) noexcept;
  [[nodiscard]] gneiss::engine::result destroy_material(gneiss::engine::rid material) noexcept;
  [[nodiscard]] GNEISS_API gneiss::engine::result
  create_texture(const texture_view& desc, gneiss::engine::rid* out_texture) noexcept;
  /** 创建已经过容器校验的内部多 Mip Texture。 */
  [[nodiscard]] gneiss::engine::result create_texture(texture_resource resource,
                                                      gneiss::engine::rid* out_texture) noexcept;
  /** 保存已经过外层容器与 Granit Manifest 检查的运行纹理。 */
  [[nodiscard]] gneiss::engine::result
  create_packaged_texture(texture_resource resource, gneiss::engine::rid* out_texture) noexcept;
  [[nodiscard]] GNEISS_API gneiss::engine::result
  destroy_texture(gneiss::engine::rid texture) noexcept;
  /** 主线程提交已验证候选；保持 RID generation，既有帧仍拥有旧数据快照。 */
  [[nodiscard]] GNEISS_API bool
  replace_texture(gneiss::engine::rid texture,
                  std::shared_ptr<const texture_resource> prepared) noexcept;
  [[nodiscard]] const mesh_resource* get_mesh(gneiss::engine::rid mesh) const noexcept;
  [[nodiscard]] const material_resource* get_material(gneiss::engine::rid material) const noexcept;
  [[nodiscard]] const texture_resource* get_texture(gneiss::engine::rid texture) const noexcept;
  [[nodiscard]] std::shared_ptr<const mesh_resource>
  share_mesh(gneiss::engine::rid mesh) const noexcept;
  [[nodiscard]] std::shared_ptr<const material_resource>
  share_material(gneiss::engine::rid material) const noexcept;
  [[nodiscard]] std::shared_ptr<const texture_resource>
  share_texture(gneiss::engine::rid texture) const noexcept;
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
  gneiss::engine::rid_table<std::shared_ptr<const mesh_resource>> meshes_;
  gneiss::engine::rid_table<std::shared_ptr<const material_resource>> materials_;
  gneiss::engine::rid_table<std::shared_ptr<const texture_resource>> textures_;
};

} // namespace gneiss::engine

#endif
