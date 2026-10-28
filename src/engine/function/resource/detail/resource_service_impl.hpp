// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/resource/detail/asset_decode.hpp"
#include "engine/function/resource/detail/asset_load_batch.hpp"
#include "engine/function/resource/resource_service.hpp"
#include "engine/resource/resource_retirement_queue.hpp"
#include "engine/resource/texture/texture_view.hpp"
#include <unordered_map>
namespace gneiss::engine {
struct material_data;
namespace detail {
struct asset_update {
  struct material_content {
    std::shared_ptr<const material_data> data;
    std::array<texture, 5> dependencies;
  };
  using content = std::variant<std::monostate, std::shared_ptr<const mesh_data>,
                               std::shared_ptr<const texture_data>, material_content>;
  render_asset_ref target;
  std::uint64_t sequence{};
  content data;
};
}
struct GNEISS_API resource_service::implementation final {
public:
  explicit implementation(render_service& render);
  mesh wrap_mesh(rid handle);
  texture wrap_texture(rid handle);
  material wrap_material(rid handle);
  void update_mesh(rid handle, const mesh_data& data) noexcept;
  void update_texture(rid handle, const texture_data& data) noexcept;
  void update_material(rid handle, const material_data& data,
                       std::array<texture, 5> dependencies) noexcept;
  std::size_t drain() noexcept;
  bool closed_{};
  void close() noexcept;

  template <typename T> std::shared_ptr<T> make(rid handle);
  template <typename Ref, typename Create> result create(Ref& output, Create create_rid) noexcept;
  render_service& render_;
  resource_retirement_queue retirement_;
  // 仅索引本服务创建的对象；查询只锁定既有控制块，不接管外部 RID。
  std::unordered_map<rid, std::weak_ptr<const mesh_resource>> meshes_;
  std::unordered_map<rid, std::weak_ptr<const texture_resource>> textures_;
  std::unordered_map<rid, std::weak_ptr<const material_resource>> materials_;
  ~implementation();
  render_service& render() const noexcept { return render_; }
  texture_prepare_profile default_profile{};
  std::unique_ptr<asset_load_batch> batch;
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
  static std::uint64_t next_update_sequence() noexcept;
  static bool claim_update(resource_service&, const render_asset_ref&, std::uint64_t) noexcept;
  static result prepare_update(resource_service&, decoded_asset, detail::asset_update&,
                               const render_asset_ref& = {}, std::uint64_t = 0U) noexcept;
  static result apply_update(resource_service&, detail::asset_update&) noexcept;

  static std::uint64_t update_sequence(resource_service&, const render_asset_ref&) noexcept;
  static result prepare_material_update(resource_service&, const material_description&,
                                        detail::asset_update&);
};
} // namespace gneiss::engine
