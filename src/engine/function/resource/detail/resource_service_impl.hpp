// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/resource/resource_service.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/resource_retirement_queue.hpp"
#include "engine/resource/texture/texture_view.hpp"
#include <unordered_map>
namespace gneiss::engine {
struct material_data;
struct resource_service::implementation final {
public:
  explicit GNEISS_API implementation(render_service& render);
  GNEISS_API mesh wrap_mesh(rid handle);
  GNEISS_API texture wrap_texture(rid handle);
  GNEISS_API material wrap_material(rid handle);
  GNEISS_API void update_mesh(rid handle, const mesh_data& data) noexcept;
  GNEISS_API void update_texture(rid handle, const texture_data& data) noexcept;
  GNEISS_API void update_material(rid handle, const material_data& data,
                                  std::array<texture, 5> dependencies) noexcept;
  GNEISS_API std::size_t drain() noexcept;
  bool closed_{};
  GNEISS_API void close() noexcept;

  template <typename T> std::shared_ptr<T> make(rid handle);
  template <typename Ref, typename Create> result<Ref> create(Create create_rid) noexcept;
  render_service& render_;
  resource_retirement_queue retirement_;
  // 仅索引本服务创建的对象；查询只锁定既有控制块，不接管外部 RID。
  std::unordered_map<rid, std::weak_ptr<const mesh_resource>> meshes_;
  std::unordered_map<rid, std::weak_ptr<const texture_resource>> textures_;
  std::unordered_map<rid, std::weak_ptr<const material_resource>> materials_;
  GNEISS_API ~implementation();
  render_service& render() const noexcept { return render_; }
  texture_prepare_profile default_profile{};
  struct pending_load;
  std::vector<std::shared_ptr<pending_load>> pending;
  std::shared_ptr<progress_notification> notification;
  bool loading_configured{};
  GNEISS_API void advance(resource_service&);
  GNEISS_API void stop_loading() noexcept;
  /** 创建新的渲染身份并返回唯一拥有者；失败返回错误，不接管外部 RID。 */
  GNEISS_API result<mesh> reserve_mesh() noexcept;
  GNEISS_API result<texture> reserve_texture() noexcept;
  GNEISS_API result<material> reserve_material() noexcept;
  GNEISS_API result<mesh> create_mesh(const mesh_view& desc) noexcept;
  GNEISS_API result<material> create_material(const material_parameters& parameters,
                                              std::array<texture, 5> dependencies) noexcept;
  GNEISS_API mesh find_mesh(rid handle) const noexcept;
  GNEISS_API material find_material(rid handle) const noexcept;
  /** 仅查询现有纹理引用；UI 等借用 RID 的调用方不得创建拥有者。 */
  GNEISS_API texture find_texture(rid handle) const noexcept;
  /** 提交 CPU 内容后更新上层对象，不等待 GPU；材质依赖由调用方显式提供。 */
  GNEISS_API bool update_mesh(const mesh& target, std::shared_ptr<const mesh_data> data) noexcept;
  GNEISS_API bool update_texture(const texture& target,
                                 std::shared_ptr<const texture_data> data) noexcept;
  GNEISS_API bool update_material(const material& target, std::shared_ptr<const material_data> data,
                                  std::array<texture, 5> dependencies) noexcept;
  GNEISS_API result<texture> create_texture(const texture_view& desc) noexcept;
  static GNEISS_API error adopt(resource_service&, std::shared_ptr<const mesh_data>, const mesh&,
                                std::span<const asset_read_record> = {}) noexcept;
  static GNEISS_API error adopt(resource_service&, std::shared_ptr<const texture_data>,
                                const texture&, std::span<const asset_read_record> = {}) noexcept;
  static GNEISS_API error adopt(resource_service&, const material_description&, const material&,
                                std::array<texture, 5>) noexcept;
  static GNEISS_API error validate_sources(resource_service&,
                                           std::span<const asset_read_record>) noexcept;
};
} // namespace gneiss::engine
