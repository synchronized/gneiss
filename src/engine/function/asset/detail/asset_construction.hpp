// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/asset/resource_cache.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/resource_retirement_queue.hpp"
#include "engine/resource/texture/texture_view.hpp"
#include <unordered_map>
namespace gneiss {

struct material_data;

struct detail::asset_construction final {
public:
  explicit GNEISS_API asset_construction(render_service& render);
  GNEISS_API ref<const gneiss::mesh> wrap_mesh(rid handle);
  GNEISS_API ref<const gneiss::texture> wrap_texture(rid handle);
  GNEISS_API ref<const gneiss::material> wrap_material(rid handle);
  GNEISS_API void update_mesh(rid handle, const mesh_data& data);
  GNEISS_API void update_texture(rid handle, const texture_data& data);
  GNEISS_API std::size_t drain() noexcept;
  bool closed_{};
  GNEISS_API void close() noexcept;

  template <typename T> std::shared_ptr<T> make(rid handle);
  template <typename Ref, typename Create> result<Ref> create(Create create_rid);
  render_service& render_;
  resource_retirement_queue retirement_;
  // 仅索引本服务创建的对象；查询只锁定既有控制块，不接管外部 RID。
  std::unordered_map<rid, std::weak_ptr<const gneiss::mesh>> meshes_;
  std::unordered_map<rid, std::weak_ptr<const gneiss::texture>> textures_;
  std::unordered_map<rid, std::weak_ptr<const gneiss::material>> materials_;
  GNEISS_API ~asset_construction();
  render_service& render() const noexcept { return render_; }
  /** 创建新的渲染身份并返回唯一拥有者；失败返回错误，不接管外部 RID。 */
  GNEISS_API result<ref<const gneiss::mesh>> reserve_mesh();
  GNEISS_API result<ref<const gneiss::texture>> reserve_texture();
  GNEISS_API result<ref<const gneiss::material>> reserve_material();
  GNEISS_API result<ref<const gneiss::mesh>> create_mesh(const mesh_view& desc);
  GNEISS_API result<ref<const gneiss::material>>
  create_material(const material_parameters& parameters,
                  std::array<ref<const gneiss::texture>, 5> dependencies);
  /** 文件装配和程序化创建共用依赖归属校验与后端数据准备。 */
  GNEISS_API result<std::shared_ptr<const material_data>>
  prepare_material(const material_parameters&,
                   const std::array<ref<const gneiss::texture>, 5>& dependencies);
  GNEISS_API ref<const gneiss::mesh> find_mesh(rid handle) const noexcept;
  GNEISS_API ref<const gneiss::material> find_material(rid handle) const noexcept;
  /** 仅查询现有纹理引用；UI 等借用 RID 的调用方不得创建拥有者。 */
  GNEISS_API ref<const gneiss::texture> find_texture(rid handle) const noexcept;
  /** 提交 CPU 内容后更新上层对象，不等待 GPU；材质依赖由调用方显式提供。 */
  GNEISS_API bool update_mesh(const ref<const gneiss::mesh>& target,
                              std::shared_ptr<const mesh_data> data);
  GNEISS_API bool update_texture(const ref<const gneiss::texture>& target,
                                 std::shared_ptr<const texture_data> data);
  GNEISS_API bool update_material(const ref<const gneiss::material>& target,
                                  std::shared_ptr<const material_data> data,
                                  std::array<ref<const gneiss::texture>, 5> dependencies);
  GNEISS_API result<ref<const gneiss::texture>> create_texture(const texture_view& desc);
  static GNEISS_API error adopt(asset_manager&, std::shared_ptr<const mesh_data>,
                                const ref<const gneiss::mesh>&,
                                std::span<const asset_read_record> = {});
  static GNEISS_API error adopt(asset_manager&, std::shared_ptr<const texture_data>,
                                const ref<const gneiss::texture>&,
                                std::span<const asset_read_record> = {});
  static GNEISS_API error adopt(asset_manager&, const material_description&,
                                const ref<const gneiss::material>&,
                                std::array<ref<const gneiss::texture>, 5>);
  static GNEISS_API error validate_sources(asset_manager&, std::span<const asset_read_record>);
};

} // namespace gneiss
