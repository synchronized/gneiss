// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_RUNTIME_INSTANCE_HPP_
#define GNEISS_SCENE_PREFAB_RUNTIME_INSTANCE_HPP_

#include "engine/core/publication_group.hpp"
#include "engine/core/reflection/property_registry.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/resource/loaders/prefab_asset_loader.hpp"
#include "engine/resource/types/prefab_description.hpp"

#include "engine/function/world/world.hpp"

#include <gneiss/engine/game_module_types.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine {

/** 一份已提交到 World 的 Prefab Runtime 投影；析构时释放全部节点、实体和资产租约。 */
class prefab_runtime_instance final {
public:
  /** 只修改私有描述，失败保持 output 不变；不读文件、不创建实体、不发送通知。 */
  [[nodiscard]] static gneiss::engine::result
  prepare_candidate_description(const gneiss::engine::prefab_description& source,
                                const gneiss::engine::registry_ref& registry,
                                std::string_view instance_uuid,
                                std::span<const gneiss::engine::prefab_property_override> overrides,
                                gneiss::engine::prefab_description& output) noexcept;
  struct node_info final {
    const gneiss::engine::prefab_author_address* address = nullptr;
    std::string_view name;
    gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
    gneiss::engine::rid node = gneiss::engine::null_rid;
    gneiss::engine::rid parent = gneiss::engine::null_rid;
  };

  prefab_runtime_instance(gneiss::engine::world& native_world, gneiss_world world,
                          gneiss::engine::render_asset_loader& loader,
                          gneiss::engine::prefab_asset_ref prefab,
                          std::string instance_uuid) noexcept;
  ~prefab_runtime_instance() noexcept;

  prefab_runtime_instance(const prefab_runtime_instance&) = delete;
  prefab_runtime_instance& operator=(const prefab_runtime_instance&) = delete;

  [[nodiscard]] static gneiss::engine::result
  create(gneiss::engine::world& native_world, gneiss_world world,
         gneiss::engine::render_asset_loader& loader, gneiss::engine::prefab_asset_ref prefab,
         const gneiss::engine::registry_ref& registry, std::string_view instance_uuid,
         gneiss::engine::rid parent, const gneiss::engine::transform& root_transform,
         const std::vector<gneiss::engine::prefab_property_override>& overrides,
         std::unique_ptr<prefab_runtime_instance>& out_instance) noexcept;

  [[nodiscard]] gneiss::engine::rid root() const noexcept { return root_node_; }
  [[nodiscard]] gneiss_entity_id root_entity() const noexcept { return root_entity_; }
  [[nodiscard]] gneiss::engine::prefab_asset_ref prefab_lease() const noexcept { return prefab_; }
  /** 仅供未发布候选的镜像准备；返回值有效期不超过实例或下一次重载。 */
  [[nodiscard]] const gneiss::engine::prefab_description& candidate_description() const noexcept {
    return prepared_ ? *prepared_ : *prefab_.get();
  }
  [[nodiscard]] gneiss::engine::rid find_node(std::string_view source_node_uuid) const noexcept;
  [[nodiscard]] gneiss_entity_id find_entity(std::string_view source_node_uuid) const noexcept;
  [[nodiscard]] const gneiss::engine::object_description*
  find_source_object(std::string_view source_node_uuid) const noexcept;
  [[nodiscard]] std::size_t node_count() const noexcept { return nodes_.size(); }
  [[nodiscard]] gneiss::engine::result get_node_info(std::size_t index,
                                                     node_info& out_info) const noexcept;
  /** 验证候选来源、覆盖与资源是否可提交，不修改 World。 */
  [[nodiscard]] gneiss::engine::result
  validate_reload(const gneiss::engine::prefab_asset_ref& candidate,
                  const gneiss::engine::registry_ref& registry,
                  const std::vector<gneiss::engine::prefab_property_override>& overrides);
  /** 保持实例根与匹配来源节点身份，提交已经验证的来源修订。 */
  [[nodiscard]] gneiss::engine::result
  reload(gneiss::engine::prefab_asset_ref candidate, const gneiss::engine::registry_ref& registry,
         const std::vector<gneiss::engine::prefab_property_override>& overrides);

  /** 候选 World 专用的分阶段构造；每次只创建一个节点，渲染资源由调用方传入租约。 */
  [[nodiscard]] gneiss::engine::result begin_staged(gneiss::engine::rid parent,
                                                    const gneiss::engine::transform& transform);
  /** 活动 World 内隐藏构造；描述已完成覆盖验证，发布前不激活相机。 */
  [[nodiscard]] gneiss::engine::result begin_hidden(gneiss::engine::rid parent,
                                                    const gneiss::engine::transform& transform,
                                                    gneiss::engine::prefab_description prepared);
  [[nodiscard]] gneiss::engine::result publish_hidden() noexcept;
  [[nodiscard]] gneiss::engine::result retire_hidden() noexcept;
  /** 旧同步实例也可加入退休组；逐项准备不改变可见性，取消可保留这些内部关联。 */
  [[nodiscard]] gneiss::engine::result prepare_retirement_step(bool& complete);
  [[nodiscard]] gneiss::engine::result
  create_staged_node(std::size_t index, gneiss::engine::rid parent,
                     gneiss::engine::render_asset_ref mesh,
                     gneiss::engine::render_asset_ref material);
  [[nodiscard]] gneiss::engine::result
  apply_staged_override(const gneiss::engine::registry_ref& registry,
                        const gneiss::engine::prefab_property_override& value) noexcept;
  /** 候选构造取消：每次最多处理 budget 个节点/记录；完成前须继续推进，禁止继续构造。
   * 不回收共享资产缓存；返回 true 表示候选节点、记录和来源租约均已释放。
   */
  [[nodiscard]] bool cancel_staged_step(std::size_t budget) noexcept;

private:
  friend class scene_instance;
  friend class prefab_candidate;
  [[nodiscard]] gneiss::engine::result commit_node(std::size_t index, gneiss::engine::rid parent);
  struct runtime_node final {
    gneiss::engine::prefab_author_address address;
    std::string name;
    gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
    gneiss::engine::rid node = gneiss::engine::null_rid;
    gneiss::engine::rid parent = gneiss::engine::null_rid;
    gneiss::engine::render_asset_ref mesh;
    gneiss::engine::render_asset_ref material;
    bool staged_attempted{};
  };

  void rollback() noexcept;
  [[nodiscard]] gneiss::engine::result
  stage_assets(const gneiss::engine::prefab_description& description);
  [[nodiscard]] gneiss::engine::result commit(const gneiss::engine::prefab_description& description,
                                              gneiss::engine::rid parent,
                                              const gneiss::engine::transform& root_transform);
  [[nodiscard]] gneiss::engine::result
  apply_overrides(const gneiss::engine::registry_ref& registry,
                  std::span<const gneiss::engine::prefab_property_override> overrides) noexcept;

  gneiss::engine::world& native_world_;
  // 仅供原生反射访问器定位；不拥有 World，不用于普通场景操作。
  gneiss_world reflection_context_;
  gneiss::engine::render_asset_loader& loader_;
  gneiss::engine::prefab_asset_ref prefab_;
  std::string instance_uuid_;
  gneiss_entity_id root_entity_ = GNEISS_NULL_ENTITY_ID;
  gneiss::engine::rid root_node_ = gneiss::engine::null_rid;
  std::vector<runtime_node> nodes_;
  std::vector<std::size_t> staged_order_;
  bool staged_{};
  bool cancelling_{};
  gneiss::engine::publication publication_;
  std::optional<gneiss::engine::prefab_description> prepared_;
  bool published_{};
  gneiss::engine::rid candidate_scene_{};
  gneiss::engine::rid candidate_previous_root_{};
  std::size_t retirement_cursor_{};
};

} // namespace gneiss::engine

#endif
