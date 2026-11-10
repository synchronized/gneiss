// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/entity_id.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/result.hpp"
#include "engine/function/scene/scene_types.hpp"

namespace gneiss::engine {
class world;
class scene_instance;
/** 运行对象的非拥有句柄；World 必须比句柄使用期更长，所有访问限所属线程。
 * 不缓存组件地址，不延长实体寿命；每次访问校验 World 与实体 generation。 */
class game_object final {
public:
  game_object() noexcept = default;
  [[nodiscard]] entity_id entity() const noexcept { return entity_; }
  [[nodiscard]] world* owner() const noexcept { return owner_; }
  [[nodiscard]] GNEISS_API bool is_valid() const noexcept;
  explicit operator bool() const noexcept { return is_valid(); }
  [[nodiscard]] GNEISS_API result<scene_node_identity_view> identity() const;
  /** 只修改运行名称，保留对象身份，不写编辑文档。 */
  [[nodiscard]] GNEISS_API error set_name(std::string_view name) const;
  [[nodiscard]] GNEISS_API error set_identity(scene_node_identity identity) const;
  [[nodiscard]] GNEISS_API result<math::transform> local_transform() const;
  /** 完整层级矩阵，保留非均匀缩放与旋转组合产生的剪切。 */
  [[nodiscard]] GNEISS_API result<math::matrix4> world_matrix() const;
  /** 借用所属场景；纯 ECS 辅助实体或失效对象返回 invalid_handle。 */
  [[nodiscard]] GNEISS_API result<scene_instance> scene() const;
  [[nodiscard]] GNEISS_API result<math::transform> world_transform() const;
  [[nodiscard]] GNEISS_API error set_local_transform(const math::transform&) const;
  /** 无父节点时返回成功的空句柄；失效对象返回 invalid_handle。 */
  [[nodiscard]] GNEISS_API result<game_object> parent() const;
  /** 空 parent 表示根；拒绝跨 World 及循环层级。 */
  [[nodiscard]] GNEISS_API error reparent(game_object parent) const;
  /** 销毁整棵子树，所有指向其中实体的借用句柄同时失效。 */
  [[nodiscard]] GNEISS_API error destroy() const;
  /** 普通组件的值副本；失效实体、错误线程和组件缺失分别返回错误。
   * 组件实现单元通过 detail/component_storage.hpp 实例化，调用方不依赖 ECS 后端。 */
  template <class T> [[nodiscard]] GNEISS_API result<T> read_component() const;
  /** 安装或替换普通组件；保留正常 C++ 构造和析构语义。 */
  template <class T> [[nodiscard]] GNEISS_API error set_component(T value) const;
  template <class T> [[nodiscard]] GNEISS_API error remove_component() const;
  friend bool operator==(game_object, game_object) noexcept = default;

private:
  friend class world;
  game_object(world& owner, entity_id entity) noexcept : owner_(&owner), entity_(entity) {}
  world* owner_{};
  entity_id entity_{};
};
}
