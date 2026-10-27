// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_RESOURCE_SERVICE_HPP_
#define GNEISS_RENDER_RENDER_RESOURCE_SERVICE_HPP_

#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/rid_table.hpp"
#include "engine/function/render/render_resource.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/function/render/resource_retirement_queue.hpp"
#include "engine/function/render/texture_view.hpp"

#include "engine/core/export.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gneiss::engine {

struct render_memory_usage {
  std::uint64_t logical_bytes{};
  std::uint64_t cpu_data_bytes{};
};

class render_resource_service final {
public:
  static constexpr std::uint64_t default_memory_limit = UINT64_C(4) * 1024U * 1024U * 1024U;
  GNEISS_API explicit render_resource_service(
      std::uint64_t memory_limit = default_memory_limit) noexcept;
  GNEISS_API ~render_resource_service();
  /** 所属线程永久关闭，晚释放引用只销毁 CPU 外壳。 */
  GNEISS_API void close() noexcept;
  /** 所属线程取得槽位的唯一运行对象；非法 RID 返回空，分配失败抛出 bad_alloc。 */
  [[nodiscard]] GNEISS_API mesh reference_mesh(rid handle);
  [[nodiscard]] GNEISS_API texture reference_texture(rid handle);
  [[nodiscard]] GNEISS_API material reference_material(rid handle);

  std::size_t drain_retired_resources() noexcept { return retirement_ ? retirement_->drain() : 0U; }
  /** 所属线程查询；跟踪共享对象，包含仍被旧帧持有的已销毁 RID 数据，不强持有对象。 */
  [[nodiscard]] render_memory_usage memory_usage() const noexcept;
  [[nodiscard]] std::uint64_t available_memory_bytes() const noexcept;

  [[nodiscard]] bool is_valid() const noexcept { return domain_ != 0U; }
  /** 所属线程预留合法 RID；尚无 CPU 内容，不可直接绘制。由 replace_* 填充，destroy_* 取消。
   * 不创建后端对象；所有权属于调用方，销毁和跨服务校验与普通 RID 一致。 */
  [[nodiscard]] GNEISS_API result reserve_mesh(rid* output) noexcept;
  [[nodiscard]] GNEISS_API result reserve_texture(rid* output) noexcept;
  [[nodiscard]] GNEISS_API result reserve_material(rid* output) noexcept;
  /** 只查询 CPU 内容：SUCCESS 已填充，NOT_READY 仅预留，INVALID_HANDLE 已失效或不属于本服务。 */
  [[nodiscard]] result mesh_status(rid mesh) const noexcept;
  [[nodiscard]] result texture_status(rid texture) const noexcept;
  [[nodiscard]] result material_status(rid material) const noexcept;
  [[nodiscard]] GNEISS_API result create_mesh(const mesh_view& desc, rid* out_mesh) noexcept;
  /** 校验并接收拥有型网格，成功路径不再复制数组。 */
  [[nodiscard]] result create_mesh(mesh_data resource, rid* output) noexcept;
  /** 内部 CPU 候选已经校验，移动所有权避免主线程复制大数组。 */
  [[nodiscard]] result create_prepared_mesh(mesh_data resource, rid* output) noexcept;
  /** 已校验不可变 CPU 网格；RID 与旧帧共享同一对象，销毁 RID 不使外部引用失效。 */
  [[nodiscard]] result create_prepared_mesh(std::shared_ptr<const mesh_data> resource,
                                            rid* output) noexcept;
  [[nodiscard]] GNEISS_API bool replace_mesh(rid rid,
                                             std::shared_ptr<const mesh_data> data) noexcept;
  [[nodiscard]] bool replace_material(rid rid, std::shared_ptr<const material_data> data) noexcept;
  /** 校验并登记临时 CPU 数据，不分配 RID；任务和帧引用共同决定数据寿命。 */
  [[nodiscard]] result prepare_mesh_data(std::shared_ptr<const mesh_data> resource,
                                         std::shared_ptr<const mesh_data>& output) noexcept;
  [[nodiscard]] result prepare_material_data(const material_data& resource,
                                             std::shared_ptr<const material_data>& output) noexcept;
  [[nodiscard]] result prepare_texture_data(texture_data resource,
                                            std::shared_ptr<const texture_data>& output) noexcept;
  [[nodiscard]] GNEISS_API result destroy_mesh(rid mesh) noexcept;
  [[nodiscard]] GNEISS_API result create_material(const material_data& value,
                                                  rid* out_material) noexcept;
  [[nodiscard]] GNEISS_API result destroy_material(rid material) noexcept;
  [[nodiscard]] GNEISS_API result create_texture(const texture_view& desc,
                                                 rid* out_texture) noexcept;
  /** 程序化纹理的强引用入口；失败清理临时槽位，成功后由引用管理销毁。 */
  [[nodiscard]] GNEISS_API result create_texture(const texture_view& desc,
                                                 texture& output) noexcept;
  /** 创建已经过容器校验的内部多 Mip Texture。 */
  [[nodiscard]] result create_texture(texture_data resource, rid* out_texture) noexcept;
  /** 保存已经过外层容器与 Granit Manifest 检查的运行纹理。 */
  [[nodiscard]] result create_packaged_texture(texture_data resource, rid* out_texture) noexcept;
  [[nodiscard]] GNEISS_API result destroy_texture(rid texture) noexcept;
  /** 主线程提交已验证候选；保持 RID generation，既有帧仍拥有旧数据快照。 */
  [[nodiscard]] GNEISS_API bool
  replace_texture(rid texture, std::shared_ptr<const texture_data> prepared) noexcept;
  [[nodiscard]] const mesh_data* get_mesh(rid mesh) const noexcept;
  [[nodiscard]] const material_data* get_material(rid material) const noexcept;
  [[nodiscard]] const texture_data* get_texture(rid texture) const noexcept;
  [[nodiscard]] std::shared_ptr<const mesh_data> share_mesh(rid mesh) const noexcept;
  [[nodiscard]] std::shared_ptr<const material_data> share_material(rid material) const noexcept;
  [[nodiscard]] std::shared_ptr<const texture_data> share_texture(rid texture) const noexcept;
  [[nodiscard]] GNEISS_API std::size_t live_resource_count() const noexcept;

private:
  /** 仅运行资源使用延迟析构；CPU 负载仍使用普通 shared_ptr。 */
  template <typename T, typename... Args> std::shared_ptr<T> make_runtime_resource(Args&&... args) {
    if (!retirement_)
      retirement_ = std::make_unique<resource_retirement_queue>(this);
    return retirement_->make<T>(
        [](T& value, void* owner) noexcept {
          // 关闭后只析构 CPU 外壳，禁止访问已销毁的服务。
          value.resources_ = static_cast<render_resource_service*>(owner);
        },
        std::forward<Args>(args)...);
  }
  template <typename Data, typename Resource> struct resource_record {
    explicit resource_record(std::shared_ptr<const Data> value) : data(std::move(value)) {}
    std::shared_ptr<const Data> data;
    std::weak_ptr<const Resource> identity;
    bool has_identity{};
  };
  template <typename Resource>
  std::shared_ptr<const Resource> track(std::shared_ptr<const Resource> resource,
                                        std::vector<std::weak_ptr<const Resource>>& history);
  std::unique_ptr<resource_retirement_queue> retirement_;
  std::uint64_t memory_limit_{};
  mutable std::vector<std::weak_ptr<const mesh_data>> mesh_history_;
  mutable std::vector<std::weak_ptr<const material_data>> material_history_;
  mutable std::vector<std::weak_ptr<const texture_data>> texture_history_;
  std::uint16_t domain_{};
  rid_table<resource_record<mesh_data, mesh_resource>> meshes_;
  rid_table<resource_record<material_data, material_resource>> materials_;
  rid_table<resource_record<texture_data, texture_resource>> textures_;
};

} // namespace gneiss::engine

#endif
