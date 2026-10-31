// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
#include "engine/function/resource/resource_request.hpp"
#include <memory>
namespace gneiss::engine {
class asset_manager;
struct mesh_view;
struct texture_view;
class render_service;
class task_executor;
class progress_notification;
/** 运行资源创建、缓存和加载装配入口；所属线程使用。
 * 借用的资产管理器、执行器及 Render 必须覆盖服务寿命。 */
class GNEISS_API resource_service final {
public:
  struct implementation; // 定义仅供服务内部实现使用。
  explicit resource_service(asset_manager& assets);
  ~resource_service();
  resource_service(const resource_service&) = delete;
  resource_service& operator=(const resource_service&) = delete;
  void advance();
  void collect_unused() noexcept;
  const resource_cache& cache() const noexcept { return cache_; }
  void request_stop() noexcept;
  bool stopped() const noexcept;
  void stop() noexcept;
  /** 借用 CPU 加载器；不转移所有权，服务停止不关闭该加载器。 */
  asset_manager& assets() const noexcept { return assets_; }
  /** 绑定渲染服务；Render 必须覆盖本服务的运行与关闭阶段。 */
  error attach_render(render_service& render, texture_prepare_profile profile = {});
  /** 配置 CPU 加载；首次配置后保留执行器与 profile，停止后拒绝重新装配。 */
  error configure_loading(task_executor& executor, texture_prepare_profile profile = {},
                          std::shared_ptr<progress_notification> notification = {});
  result<render_resource_ref> reserve_resource(const render_resource_source&) noexcept;
  /** 只查找 CPU 内容已就绪的缓存，不读取文件、不等待 GPU。 */
  result<render_resource_ref> find(const render_resource_source&,
                                   texture_prepare_profile = {}) const noexcept;
  template <class T> result<asset_ref<const T>> find(std::string_view uri) const noexcept try {
    auto found = find({std::string(uri), kind<T>()}, profile());
    if (!found)
      return found.error();
    return resource_cast<T>(*found);
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
  bool owns(const mesh&) const noexcept;
  bool owns(const texture&) const noexcept;
  bool owns(const material&) const noexcept;
  bool owns(const render_resource_ref&) const noexcept;
  /** 所属线程创建程序化资源并返回强引用；失败返回错误，不接管外部 RID。 */
  result<texture> create_texture(const texture_view&) noexcept;
  result<mesh> create_mesh(const mesh_view&) noexcept;
  result<material> create_material(const material_parameters&, std::array<texture, 5>) noexcept;
  result<texture> reserve_texture() noexcept;
  result<mesh> reserve_mesh() noexcept;
  result<material> reserve_material() noexcept;
  /** 仅锁定本管理器已创建的对象；未知或已释放 RID 返回空。 */
  texture find_texture(rid) const noexcept;
  mesh find_mesh(rid) const noexcept;
  material find_material(rid) const noexcept;
  /** 单资源异步加载；默认复用缓存，同身份的在途请求共享工作（包括 reload）。
   * 输入在返回前复制；提交失败返回错误。每项独立完成，不等待 GPU。 */
  result<resource_request> load(const render_resource_source&, bool reload = false);
  template <class T> result<resource_request> load(std::string_view uri, bool reload = false) try {
    return load({std::string(uri), kind<T>()}, reload);
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }

  texture_prepare_profile profile() const noexcept;

private:
  template <class T> static constexpr render_resource_kind kind() {
    static_assert(std::is_same_v<T, mesh_resource> || std::is_same_v<T, texture_resource> ||
                  std::is_same_v<T, material_resource>);
    if constexpr (std::is_same_v<T, mesh_resource>)
      return render_resource_kind::mesh;
    else if constexpr (std::is_same_v<T, texture_resource>)
      return render_resource_kind::texture;
    else
      return render_resource_kind::material;
  }
  result<asset_identity> identify(std::string_view, std::uint32_t) const noexcept;
  asset_manager& assets_;
  bool render_stopping_{};
  std::unique_ptr<implementation> impl_;
  resource_cache cache_;
};
}
