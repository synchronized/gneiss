// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/material/material_resource.hpp"
#include "engine/resource/mesh/mesh_resource.hpp"
#include "engine/resource/resource_retirement_queue.hpp"
#include <unordered_map>
namespace gneiss::engine {
class render_service;
struct mesh_data;
struct texture_data;
struct material_data;
/** Render 生命周期内的资源身份关联。只索引 RID，不承担 URI 缓存或 CPU 数据表。 */
class resource_bindings final {
public:
  explicit resource_bindings(render_service& render) : render_(render), retirement_(&render) {}
  mesh reference_mesh(rid handle);
  texture reference_texture(rid handle);
  material reference_material(rid handle);
  void update_mesh(rid handle, const mesh_data& data) noexcept;
  void update_texture(rid handle, const texture_data& data) noexcept;
  bool prepare_material(rid handle, const material_data& data,
                        std::array<texture, 5>& dependencies);
  void update_material(rid handle, const material_data& data,
                       std::array<texture, 5> dependencies) noexcept;
  void forget_mesh(rid handle) noexcept { meshes_.erase(handle); }
  void forget_texture(rid handle) noexcept { textures_.erase(handle); }
  void forget_material(rid handle) noexcept { materials_.erase(handle); }
  std::size_t drain() noexcept { return retirement_.drain(); }
  void close() noexcept;
  static resource_bindings& of(render_service& render) noexcept;

private:
  template <typename T> std::shared_ptr<T> make(rid handle);
  render_service& render_;
  resource_retirement_queue retirement_;
  // 存在但 weak 已过期意味着等待销毁，禁止为同一个 RID 复活第二个拥有者。
  std::unordered_map<rid, std::weak_ptr<const mesh_resource>> meshes_;
  std::unordered_map<rid, std::weak_ptr<const texture_resource>> textures_;
  std::unordered_map<rid, std::weak_ptr<const material_resource>> materials_;
};
} // namespace gneiss::engine
