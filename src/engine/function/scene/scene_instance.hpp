// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/game_object.hpp"
#include <functional>
namespace gneiss::engine {
/** 场景的非拥有句柄；World 必须比使用期更长，全部访问限所属线程。 */
class scene_instance final {
public:
  scene_instance() noexcept = default;
  /** 借用既有场景；拒绝失效、跨 World 或跨线程访问。 */
  [[nodiscard]] static GNEISS_API result<scene_instance> borrow(world&, scene_instance_id) noexcept;
  [[nodiscard]] world* owner() const noexcept { return owner_; }
  [[nodiscard]] scene_instance_id id() const noexcept { return id_; }
  [[nodiscard]] GNEISS_API bool is_valid() const noexcept;
  [[nodiscard]] GNEISS_API result<std::uint64_t> size() const noexcept;
  /** 查询对象是否属于本场景；跨 World、空或失效对象返回 false。 */
  [[nodiscard]] GNEISS_API result<bool> contains(game_object) const noexcept;
  /** 创建本场景成员；父对象必须属于本场景，空父对象创建根。 */
  [[nodiscard]] GNEISS_API result<game_object>
  create(scene_node_identity, const math::transform& = {}, game_object parent = {}) const;
  /** 所属线程同步遍历成员；回调不得创建/删除实体、改变层级或归属，顺序不保证稳定。
   * 回调失败停止后续调用，不缓存成员副本；roots_only 只访问根对象。 */
  [[nodiscard]] GNEISS_API error visit_objects(const std::function<error(game_object)>&,
                                               bool roots_only = false) const;
  /** 卸载全部成员；复制的场景/对象借用句柄不阻止卸载。 */
  [[nodiscard]] GNEISS_API error unload() const noexcept;

private:
  world* owner_{};
  scene_instance_id id_{};
};
}
