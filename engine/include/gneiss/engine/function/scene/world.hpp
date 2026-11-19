// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <functional>
#include <gneiss/engine/core/base/frame_time.hpp>
#include <gneiss/engine/core/base/result.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/core/events/signal.hpp>
#include <gneiss/engine/export.hpp>
#include <gneiss/engine/function/scene/entity_id.hpp>
#include <gneiss/engine/function/scene/game_object.hpp>
#include <gneiss/engine/function/scene/object_change.hpp>
#include <gneiss/engine/function/scene/scene_instance.hpp>
#include <gneiss/engine/function/scene/scene_types.hpp>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace gneiss {
struct world_state;
}
namespace gneiss {
/** 原生 World 操作入口；拥有唯一 ECS/场景树状态，全部访问限所属线程。
 * domain 由 World 创建工厂分配；私有实现不向宿主暴露 EnTT。 */
class world {
public:
  /** 注册本 World 拥有的可复制 callable；按注册顺序执行，首次 update 冻结列表。
   * 签名为 (world&, const frame_time&)，返回 void/error/result<void>。
   * 引用捕获由调用方保证寿命；重名、错线程、冻结后注册属于契约违例。 */
  template <class F> void add_system(std::string name, F&& function) {
    using callable = std::decay_t<F>;
    static_assert(std::is_copy_constructible_v<callable>, "World 系统 callable 必须可复制");
    register_system(std::move(name),
                    [function = callable(std::forward<F>(function))](
                        world& target, const frame_time& time) mutable -> error {
                      using R = std::invoke_result_t<callable&, world&, const frame_time&>;
                      if constexpr (std::is_void_v<R>) {
                        std::invoke(function, target, time);
                        return error::success;
                      } else if constexpr (std::is_same_v<R, error>) {
                        return std::invoke(function, target, time);
                      } else {
                        static_assert(std::is_same_v<R, result<void>>,
                                      "World 系统返回类型必须为 void/error/result<void>");
                        auto status = std::invoke(function, target, time);
                        return status ? error::success : status.error();
                      }
                    });
  }
  /** 所属线程顺序执行；失败短路并保留此前修改，意外异常传播。禁止重入及执行中销毁。
   * 不推进共享服务、不广播变化或提交渲染；宿主决定哪些 World 更新。 */
  [[nodiscard]] GNEISS_ENGINE_API result<void> update(const frame_time& time);

  world(const world&) = delete;
  world& operator=(const world&) = delete;
  [[nodiscard]] GNEISS_ENGINE_API bool is_owner_thread() const noexcept;
  /** 批次仅在回调期间借用；回调只接收记录，不得修改 World。 */
  [[nodiscard]] GNEISS_ENGINE_API events::sink<void(const object_changes&)> changes() const;
  /** 宿主在本轮修改后于所属线程调用；独立于 Render，异常不作为可重试广播事务。 */
  GNEISS_ENGINE_API void flush_changes();
  /** 创建空运行场景；不需要描述身份，返回非拥有句柄。 */
  [[nodiscard]] GNEISS_ENGINE_API result<scene_instance> create_scene();
  /** 将整棵对象子树移入本 World 的目标场景；保持局部变换与实体身份。
   * parent 必须属于目标且不在子树中；验证失败不改变归属或层级。 */
  [[nodiscard]] GNEISS_ENGINE_API error transfer_subtree(game_object root,
                                                         scene_instance destination,
                                                         game_object parent = {});
  /** 返回全部 ECS 实体数量；必须在所属线程调用，违例断言终止。 */
  [[nodiscard]] GNEISS_ENGINE_API std::uint64_t entity_count() const;
  /** 构造借用句柄；不访问 ECS，各操作在使用时校验身份和线程。零 ID 返回空句柄。 */
  [[nodiscard]] game_object find(entity_id entity) noexcept {
    return entity.get() ? game_object{*this, entity} : game_object{};
  }
  /** 将本 World 中有层级组件的实体借用为对象，纯 ECS 实体返回 not_found。 */
  [[nodiscard]] GNEISS_ENGINE_API result<game_object> object(entity_id entity) noexcept;

private:
  friend struct world_state;
  friend class scene_instance;
  [[nodiscard]] error validate_scene(scene_instance_id) const noexcept;
  GNEISS_ENGINE_API void register_system(std::string name,
                                         std::function<error(world&, const frame_time&)> function);
  world() = default;
  ~world() = default;
};
}

namespace gneiss {
/** 将实现类型的析构留在 World 模块，不向场景域传播 ECS 后端头。 */
struct world_deleter {
  GNEISS_ENGINE_API void operator()(gneiss::world* state) const noexcept;
};
using world_owner = std::unique_ptr<gneiss::world, world_deleter>;
/** 创建独立 World 的拥有者；必须在所属线程析构。 */
[[nodiscard]] GNEISS_ENGINE_API result<world_owner> create_native();
}
