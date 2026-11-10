// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/entity_id.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/render/render_snapshot.hpp"
#include "engine/function/scene/game_object.hpp"
#include "engine/function/scene/scene_creation.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/scene_types.hpp"
#include <memory>
#include <span>
#include <vector>

namespace gneiss::engine {
class world_state;
struct world_factory;
namespace detail {
struct world_component_access;
}
}
namespace gneiss::engine {
/** 原生 World 操作入口；拥有唯一 ECS/场景树状态，全部访问限所属线程。
 * domain 由 World 创建工厂分配；私有实现不向宿主暴露 EnTT。 */
class world final {
public:
  GNEISS_API ~world() noexcept;
  world(const world&) = delete;
  world& operator=(const world&) = delete;
  [[nodiscard]] GNEISS_API bool is_owner_thread() const noexcept;
  /** 创建空运行场景；不需要描述身份，返回非拥有句柄。 */
  [[nodiscard]] GNEISS_API result<scene_instance> create_scene();
  /** 将整棵对象子树移入本 World 的目标场景；保持局部变换与实体身份。
   * parent 必须属于目标且不在子树中；验证失败不改变归属或层级。 */
  [[nodiscard]] GNEISS_API error transfer_subtree(game_object root, scene_instance destination,
                                                  game_object parent = {});
  [[nodiscard]] GNEISS_API error entity_create(entity_id& out_entity);
  [[nodiscard]] GNEISS_API error entity_destroy(entity_id entity);
  [[nodiscard]] GNEISS_API result<bool> entity_is_alive(entity_id entity);
  [[nodiscard]] GNEISS_API result<std::uint64_t> entity_count();
  [[nodiscard]] GNEISS_API error set_active_camera(entity_id entity);
  [[nodiscard]] GNEISS_API error get_active_camera(entity_id& out_entity);
  /** 创建有层级的实体；parent 为空时为根，失败不留下部分对象。 */
  [[nodiscard]] GNEISS_API result<game_object> create_object(scene_node_identity identity,
                                                             const math::transform& transform = {},
                                                             game_object parent = {});
  /** 构造借用句柄；不访问 ECS，各操作在使用时校验身份和线程。零 ID 返回空句柄。 */
  [[nodiscard]] game_object find(entity_id entity) noexcept {
    return entity.get() ? game_object{*this, entity} : game_object{};
  }
  /** 将本 World 中有层级组件的实体借用为对象，纯 ECS 实体返回 not_found。 */
  [[nodiscard]] GNEISS_API result<game_object> object(entity_id entity) noexcept;

  [[nodiscard]] GNEISS_API error entity_get_local_transform(entity_id entity,
                                                            math::transform& out_transform);
  [[nodiscard]] GNEISS_API error entity_set_local_transform(entity_id entity,
                                                            const math::transform& transform);
  [[nodiscard]] GNEISS_API error get_render_snapshot(std::uint32_t viewport_width,
                                                     std::uint32_t viewport_height,
                                                     render_snapshot& out_snapshot,
                                                     const view_camera* camera);

private:
  friend struct engine::world_factory;
  friend class scene_instance;
  [[nodiscard]] error validate_scene(scene_instance_id) const noexcept;
  [[nodiscard]] error unload_scene(scene_instance_id) noexcept;
  [[nodiscard]] result<std::uint64_t> scene_size(scene_instance_id) const;
  friend struct detail::world_component_access;
  explicit GNEISS_API world(std::uint32_t domain);
  std::unique_ptr<engine::world_state> state_;
};
}

namespace gneiss::engine {
/** 将实现类型的析构留在 World 模块，不向场景域传播 ECS 后端头。 */
struct world_deleter {
  GNEISS_API void operator()(engine::world* state) const noexcept;
};
using world_owner = std::unique_ptr<engine::world, world_deleter>;
/** 创建独立 World 的拥有者；必须在所属线程析构。 */
[[nodiscard]] GNEISS_API result<world_owner> create_native();
}
