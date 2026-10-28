// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
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
  result attach_render(render_service& render, texture_prepare_profile profile = {});
  /** 配置 CPU 加载；首次配置后保留执行器与 profile，停止后拒绝重新装配。 */
  result configure_loading(task_executor& executor, texture_prepare_profile profile = {},
                           std::shared_ptr<progress_notification> notification = {});
  static constexpr std::size_t maximum_prepared_bytes = 256U * 1024U * 1024U;
  result reserve_asset(const asset_source&, render_asset_ref&) noexcept;
  result acquire_cached(const asset_source&, render_asset_ref&,
                        texture_prepare_profile = {}) const noexcept;
  result acquire_asset(asset_type, std::string_view, render_asset_ref&, asset_diagnostic&) noexcept;
  result acquire_mesh(std::string_view, mesh&, asset_diagnostic&) noexcept;
  result acquire_texture(std::string_view, texture&, asset_diagnostic&) noexcept;
  result acquire_material(std::string_view, material&, asset_diagnostic&) noexcept;
  static mesh mesh_ref(const render_asset_ref& value) noexcept {
    return asset_cast<mesh_resource>(value);
  }
  static material material_ref(const render_asset_ref& value) noexcept {
    return asset_cast<material_resource>(value);
  }
  bool owns(const mesh&) const noexcept;
  bool owns(const texture&) const noexcept;
  bool owns(const material&) const noexcept;
  bool owns(const render_asset_ref&) const noexcept;
  /** 所属线程创建程序化资源并返回强引用；失败清空输出，不接管外部 RID。 */
  result create_texture(const texture_view&, texture&) noexcept;
  result create_mesh(const mesh_view&, mesh&) noexcept;
  result create_material(const material_parameters&, std::array<texture, 5>, material&) noexcept;
  result reserve_texture(texture&) noexcept;
  result reserve_mesh(mesh&) noexcept;
  result reserve_material(material&) noexcept;
  /** 仅锁定本管理器已创建的对象；未知或已释放 RID 返回空。 */
  texture find_texture(rid) const noexcept;
  mesh find_mesh(rid) const noexcept;
  material find_material(rid) const noexcept;
  /** 单资源异步加载；默认复用缓存，同身份的在途请求共享工作（包括 reload）。
   * 输入在返回前复制；失败不替换 output。每项独立完成，不等待 GPU。 */
  result load(const asset_source&, resource_request& output, bool reload = false,
              std::size_t limit = 64U * 1024U * 1024U);
  template <class T>
  result load(std::string_view uri, resource_request& output, bool reload = false) {
    static_assert(std::is_same_v<T, mesh_resource> || std::is_same_v<T, texture_resource> ||
                  std::is_same_v<T, material_resource>);
    constexpr auto type = std::is_same_v<T, mesh_resource>      ? asset_type::mesh
                          : std::is_same_v<T, texture_resource> ? asset_type::texture
                                                                : asset_type::material;
    return load({std::string(uri), type}, output, reload);
  }
  texture_prepare_profile profile() const noexcept;

private:
  result identify(std::string_view, std::uint32_t, asset_identity&) const noexcept;
  asset_manager& assets_;
  bool render_stopping_{};
  std::unique_ptr<implementation> impl_;
  resource_cache cache_;
};
}
