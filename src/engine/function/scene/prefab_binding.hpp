// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_BINDING_HPP_
#define GNEISS_SCENE_PREFAB_BINDING_HPP_

#include "engine/core/error.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/resource/resource_service.hpp"
#include "engine/resource/scene/prefab_description.hpp"

#include "engine/function/scene/scene_object.hpp"
#include "engine/function/world/world.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine {

/** 只修改私有描述，失败保持 output 不变；不读文件、不创建实体、不发送通知。 */
[[nodiscard]] error prepare_prefab_description(const prefab_description& source,
                                               const meta::registry_ref& registry,
                                               std::span<const prefab_property_override> overrides,
                                               prefab_description& output) noexcept;

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

  prefab_binding(world& native_world, resource_service& loader, prefab_asset_ref prefab,
                 std::string instance_uuid) noexcept;
  ~prefab_binding() noexcept = default;

  prefab_binding(const prefab_binding&) = delete;
  prefab_binding& operator=(const prefab_binding&) = delete;

  [[nodiscard]] scene_node_id root() const noexcept { return root_node_; }
  [[nodiscard]] prefab_asset_ref prefab_lease() const noexcept { return prefab_; }
  [[nodiscard]] scene_node_id find_node(std::string_view source_node_uuid) const noexcept;
  [[nodiscard]] const object_description*
  find_source_object(std::string_view source_node_uuid) const noexcept;
  [[nodiscard]] std::size_t node_count() const noexcept { return nodes_.size(); }
  [[nodiscard]] error get_node_info(std::size_t index, node_info& out_info) const noexcept;
  /** 验证候选来源、覆盖与资源是否可提交，不修改 World。 */
  [[nodiscard]] error validate_reload(const prefab_asset_ref& candidate,
                                      const meta::registry_ref& registry,
                                      const std::vector<prefab_property_override>& overrides);
  /** 保持实例根与匹配来源节点身份，提交已经验证的来源修订。 */
  [[nodiscard]] error reload(prefab_asset_ref candidate, const meta::registry_ref& registry,
                             const std::vector<prefab_property_override>& overrides);

  /** 同步构造的内部步骤；节点直接可见，资源由调用方传入，失败由调用方的子树守卫清理。 */
  [[nodiscard]] error create_root(scene_node_id parent, const math::transform& transform);
  [[nodiscard]] error create_node(std::size_t index, const object_description& source,
                                  scene_node_id parent, engine::mesh mesh,
                                  engine::material material);
  [[nodiscard]] error apply_override(const meta::registry_ref& registry,
                                     const prefab_property_override& value) noexcept;

private:
  friend class scene_instance;
  struct runtime_node final {
    prefab_author_address address;
    scene_node_id node = null_scene_node_id;
    bool creation_attempted{};
  };

  [[nodiscard]] error apply_overrides(const meta::registry_ref& registry,
                                      std::span<const prefab_property_override> overrides) noexcept;

  world& native_world_;
  resource_service& loader_;
  prefab_asset_ref prefab_;
  std::string instance_uuid_;
  scene_node_id root_node_ = null_scene_node_id;
  std::vector<runtime_node> nodes_;
};

} // namespace gneiss::engine

#endif
