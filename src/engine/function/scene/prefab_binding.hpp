// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_BINDING_HPP_
#define GNEISS_SCENE_PREFAB_BINDING_HPP_

#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/resource/prefab.hpp"
#include "engine/resource/scene/prefab_description.hpp"

#include "engine/function/scene/scene_object.hpp"
#include "engine/function/world/world.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine {

/** 场景私有的 Prefab 来源绑定及映射操作，不是独立场景对象。
 * 只持有来源资源；World、资源服务和节点均为借用。构造准备数据由调用方持有，
 * 创建失败由调用方回收节点；销毁绑定本身只释放来源引用。 */
class prefab_binding final {
public:
  struct node_info final {
    const prefab_author_address* address = nullptr;
    std::string_view name;
    std::uint64_t entity = UINT64_C(0);
    scene_node_id node = null_scene_node_id;
    scene_node_id parent = null_scene_node_id;
  };

  prefab_binding(world& native_world, asset_manager& loader, prefab_asset_ref prefab,
                 guid instance_uuid, ref<const engine::prefab> source_asset = {}) noexcept;
  ~prefab_binding() noexcept = default;

  prefab_binding(const prefab_binding&) = delete;
  prefab_binding& operator=(const prefab_binding&) = delete;

  [[nodiscard]] scene_node_id root() const noexcept { return root_node_; }
  [[nodiscard]] prefab_asset_ref prefab_lease() const noexcept { return prefab_; }
  [[nodiscard]] ref<const engine::prefab> source_asset() const noexcept { return source_asset_; }
  [[nodiscard]] scene_node_id find_node(guid source_node_uuid) const noexcept;
  [[nodiscard]] const game_object_description*
  find_source_object(guid source_node_uuid) const noexcept;
  [[nodiscard]] std::size_t node_count() const noexcept { return nodes_.size(); }
  [[nodiscard]] error get_node_info(std::size_t index, node_info& out_info) const noexcept;
  /** 验证已组合的有效描述与资源；不解释覆盖，不修改 World。 */
  [[nodiscard]] error validate_reload(const prefab_asset_ref& candidate,
                                      const prefab_description& effective,
                                      scene_diagnostic* diagnostic = nullptr);
  /** 保持实例根与匹配来源节点身份，提交已经验证的来源修订。 */
  [[nodiscard]] error reload(prefab_asset_ref candidate, const prefab_description& effective);

  /** 同步构造的内部步骤；节点直接可见，资源由调用方传入，失败由调用方的子树守卫清理。 */
  [[nodiscard]] error create_root(scene_node_id parent, const math::transform& transform);
  [[nodiscard]] error create_node(std::size_t index, const game_object_description& source,
                                  scene_node_id parent, ref<const engine::mesh> mesh,
                                  ref<const engine::material> material);

private:
  friend class scene_instance;
  struct runtime_node final {
    prefab_author_address address;
    scene_node_id node = null_scene_node_id;
    bool creation_attempted{};
  };

  world& native_world_;
  asset_manager& loader_;
  prefab_asset_ref prefab_;
  ref<const engine::prefab> source_asset_;
  guid instance_uuid_;
  prefab_reference_description reference_; // 实例来源与覆盖的唯一可变记录。
  scene_node_id root_node_ = null_scene_node_id;
  std::vector<runtime_node> nodes_;
};

} // namespace gneiss::engine

#endif
