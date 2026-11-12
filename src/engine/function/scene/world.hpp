// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/entity_id.hpp"
#include "engine/core/events/signal.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/game_object.hpp"
#include "engine/function/scene/object_change.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/scene_types.hpp"
#include <memory>
#include <span>
#include <vector>

namespace gneiss {
struct world_factory;
namespace detail {
struct world_component_access;
}
}
namespace gneiss {
/** 原生 World 操作入口；拥有唯一 ECS/场景树状态，全部访问限所属线程。
 * domain 由 World 创建工厂分配；私有实现不向宿主暴露 EnTT。 */
class world final {
public:
  GNEISS_API ~world() noexcept;
  world(const world&) = delete;
  world& operator=(const world&) = delete;
  [[nodiscard]] GNEISS_API bool is_owner_thread() const noexcept;
  /** 批次仅在回调期间借用；回调只接收记录，不得修改 World。 */
  [[nodiscard]] GNEISS_API events::sink<void(const object_changes&)> changes() const;
  /** 宿主在本轮修改后调用；独立于 Render，异常不作为可重试广播事务。 */
  [[nodiscard]] GNEISS_API result<void> flush_changes();
  /** 创建空运行场景；不需要描述身份，返回非拥有句柄。 */
  [[nodiscard]] GNEISS_API result<scene_instance> create_scene();
  /** 将整棵对象子树移入本 World 的目标场景；保持局部变换与实体身份。
   * parent 必须属于目标且不在子树中；验证失败不改变归属或层级。 */
  [[nodiscard]] GNEISS_API error transfer_subtree(game_object root, scene_instance destination,
                                                  game_object parent = {});
  [[nodiscard]] GNEISS_API result<std::uint64_t> entity_count();
  /** 构造借用句柄；不访问 ECS，各操作在使用时校验身份和线程。零 ID 返回空句柄。 */
  [[nodiscard]] game_object find(entity_id entity) noexcept {
    return entity.get() ? game_object{*this, entity} : game_object{};
  }
  /** 将本 World 中有层级组件的实体借用为对象，纯 ECS 实体返回 not_found。 */
  [[nodiscard]] GNEISS_API result<game_object> object(entity_id entity) noexcept;

private:
  friend struct gneiss::world_factory;
  friend class scene_instance;
  [[nodiscard]] error validate_scene(scene_instance_id) const noexcept;
  [[nodiscard]] error unload_scene(scene_instance_id) noexcept;
  [[nodiscard]] result<std::uint64_t> scene_size(scene_instance_id) const;
  friend struct detail::world_component_access;
  explicit GNEISS_API world(std::uint32_t domain);
  struct implementation;
  std::unique_ptr<implementation> impl_;
};
}

namespace gneiss {
/** 将实现类型的析构留在 World 模块，不向场景域传播 ECS 后端头。 */
struct world_deleter {
  GNEISS_API void operator()(gneiss::world* state) const noexcept;
};
using world_owner = std::unique_ptr<gneiss::world, world_deleter>;
/** 创建独立 World 的拥有者；必须在所属线程析构。 */
[[nodiscard]] GNEISS_API result<world_owner> create_native();
}
