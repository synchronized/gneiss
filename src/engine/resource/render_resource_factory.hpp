// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/result.hpp"
#include "engine/resource/material/material_resource.hpp"
#include "engine/resource/mesh/mesh_resource.hpp"
#include "engine/resource/texture/texture_view.hpp"
#include <memory>
namespace gneiss::engine {
class render_service;
struct mesh_data;
struct mesh_view;
struct texture_data;
struct material_data;
/** 资源层装配上下文；宿主须在 Render 之前关闭/销毁，所有操作限所属线程。
 * 管理强引用的唯一包装和销毁队列，不管理 URI 缓存或 GPU 对象。 */
class GNEISS_API render_resource_factory final {
public:
  explicit render_resource_factory(render_service& render);
  ~render_resource_factory();
  render_resource_factory(const render_resource_factory&) = delete;
  render_resource_factory& operator=(const render_resource_factory&) = delete;
  [[nodiscard]] render_service& render() const noexcept;
  /** 创建新的渲染身份并返回唯一拥有者；失败输出为空，不接管外部 RID。 */
  result reserve_mesh(mesh& output) noexcept;
  result reserve_texture(texture& output) noexcept;
  result reserve_material(material& output) noexcept;
  result create_mesh(const mesh_view& desc, mesh& output) noexcept;
  result create_material(const material_parameters& parameters, std::array<texture, 5> dependencies,
                         material& output) noexcept;
  mesh find_mesh(rid handle) const noexcept;
  material find_material(rid handle) const noexcept;
  /** 仅查询现有纹理引用；UI 等借用 RID 的调用方不得创建拥有者。 */
  texture find_texture(rid handle) const noexcept;
  /** 提交 CPU 内容后更新上层对象，不等待 GPU；材质依赖由调用方显式提供。 */
  bool update_mesh(const mesh& target, std::shared_ptr<const mesh_data> data) noexcept;
  bool update_texture(const texture& target, std::shared_ptr<const texture_data> data) noexcept;
  bool update_material(const material& target, std::shared_ptr<const material_data> data,
                       std::array<texture, 5> dependencies) noexcept;
  result create_texture(const texture_view& desc, texture& output) noexcept;
  std::size_t drain() noexcept;
  /** 先排空并断开删除器端点，再销毁仍被引用的 RID；晚释放只析构 CPU 外壳。 */
  void close() noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};
[[nodiscard]] GNEISS_API result create_texture(render_resource_factory& factory,
                                               const texture_view& desc, texture& output) noexcept;
} // namespace gneiss::engine
