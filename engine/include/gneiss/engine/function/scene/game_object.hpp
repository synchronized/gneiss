// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/types/entity_id.hpp>
#include <gneiss/engine/core/math/transform.hpp>
#include <gneiss/engine/result.hpp>
#include <gneiss/engine/function/scene/scene_types.hpp>
#include <utility>
#include <vector>

namespace gneiss {
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
  /** 借用名称；要求对象有效且在所属线程访问，结构修改后须重新获取。 */
  [[nodiscard]] GNEISS_API std::string_view name() const;
  /** 只修改运行名称，保留对象身份，不写编辑文档。 */
  [[nodiscard]] GNEISS_API error set_name(std::string_view name) const;
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
  /** 返回子对象句柄快照；修改层级不改变快照，使用前须检查句柄是否仍有效。 */
  [[nodiscard]] GNEISS_API result<std::vector<game_object>> children() const;
  /** 借用普通组件；要求有效对象和所属线程，缺失返回 nullptr。
   * 任何组件增删、替换、对象销毁或场景卸载后须重新获取；复制对象句柄不保活组件。
   * 修改后调用 notify_component_modified<T>()；受约束组件仍通过自身方法校验。 */
  template <class T> [[nodiscard]] GNEISS_API T* try_component() const;
  /** 新增组件；重复类型返回 invalid_state，不覆盖原值。 */
  template <class T, class... Args> [[nodiscard]] error add_component(Args&&... args) const {
    return insert_component<T>(T{std::forward<Args>(args)...});
  }
  template <class T> [[nodiscard]] GNEISS_API error remove_component() const;
  /** 借用修改后显式通知同步消费者；组件缺失返回 not_found，增删已自动通知。 */
  template <class T> [[nodiscard]] GNEISS_API error notify_component_modified() const;
  friend bool operator==(game_object, game_object) noexcept = default;

private:
  template <class T> [[nodiscard]] GNEISS_API error insert_component(T value) const;
  friend class world;
  game_object(world& owner, entity_id entity) noexcept : owner_(&owner), entity_(entity) {}
  world* owner_{};
  entity_id entity_{};
};
}
