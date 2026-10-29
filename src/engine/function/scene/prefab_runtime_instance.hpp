// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_RUNTIME_INSTANCE_HPP_
#define GNEISS_SCENE_PREFAB_RUNTIME_INSTANCE_HPP_

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

/** 一份已提交到 World 的 Prefab Runtime 投影；析构时释放全部节点、实体和资产租约。 */
class prefab_runtime_instance final {
public:
  /** 只修改私有描述，失败保持 output 不变；不读文件、不创建实体、不发送通知。 */
  [[nodiscard]] static error
  prepare_description(const prefab_description& source, const meta::registry_ref& registry,
                      std::string_view instance_uuid,
                      std::span<const prefab_property_override> overrides,
                      prefab_description& output) noexcept;
  struct node_info final {
    const prefab_author_address* address = nullptr;
    std::string_view name;
    std::uint64_t entity = UINT64_C(0);
    scene_node_id node = null_scene_node_id;
    scene_node_id parent = null_scene_node_id;
  };

  prefab_runtime_instance(world& native_world, resource_service& loader, prefab_asset_ref prefab,
                          std::string instance_uuid) noexcept;
  ~prefab_runtime_instance() noexcept;

  prefab_runtime_instance(const prefab_runtime_instance&) = delete;
  prefab_runtime_instance& operator=(const prefab_runtime_instance&) = delete;

  [[nodiscard]] static error
  create(world& native_world, resource_service& loader, prefab_asset_ref prefab,
         const meta::registry_ref& registry, std::string_view instance_uuid, scene_node_id parent,
         const math::transform& root_transform,
         const std::vector<prefab_property_override>& overrides,
         std::unique_ptr<prefab_runtime_instance>& out_instance) noexcept;

  [[nodiscard]] scene_node_id root() const noexcept { return root_node_; }
  [[nodiscard]] std::uint64_t root_entity() const noexcept {
    return scene_object_entity(native_world_, root_node_);
  }
  [[nodiscard]] prefab_asset_ref prefab_lease() const noexcept { return prefab_; }
  [[nodiscard]] scene_node_id find_node(std::string_view source_node_uuid) const noexcept;
  [[nodiscard]] std::uint64_t find_entity(std::string_view source_node_uuid) const noexcept;
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

  /** 同步构造的内部步骤；节点直接可见，资源由调用方传入，失败由实例析构清理。 */
  [[nodiscard]] error create_root(scene_node_id parent, const math::transform& transform);
  [[nodiscard]] error create_node(std::size_t index, scene_node_id parent, engine::mesh mesh,
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

  void rollback() noexcept;
  [[nodiscard]] error apply_overrides(const meta::registry_ref& registry,
                                      std::span<const prefab_property_override> overrides) noexcept;

  world& native_world_;
  resource_service& loader_;
  prefab_asset_ref prefab_;
  std::string instance_uuid_;
  scene_node_id root_node_ = null_scene_node_id;
  std::vector<runtime_node> nodes_;
  std::optional<prefab_description> prepared_;
};

} // namespace gneiss::engine

#endif
