// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/scene_instance_service.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"
#include <gneiss/engine/application.h>
#include <map>

namespace gneiss::runtime {
class application;
}
namespace gneiss::runtime {
/** 历史持有的不可变来源与实际资产租约，不保存隐藏实例。 */
struct prefab_placement_snapshot {
  engine::resource::prefab_instance_description author;
  engine::resource::prefab_asset_lease prefab;
  std::vector<std::size_t> order;
  std::vector<engine::scene::scene_mesh_binding> bindings;
  std::map<std::string, engine::render::render_asset_lease, std::less<>> assets;
};
class GNEISS_API prefab_placement final {
public:
  /** 每次捕获最多 64 个节点；完整之前不得用于构造或删除提交。 */
  static gneiss_result capture_step(runtime::application& application, std::string_view uuid,
                                    gneiss_scene_instance scene,
                                    std::shared_ptr<prefab_placement_snapshot>& snapshot,
                                    bool& complete) noexcept;
  static gneiss_result freeze(runtime::application& application,
                              engine::scene::prepared_prefab_description prepared,
                              engine::resource::prefab_instance_description author,
                              std::vector<engine::render::render_asset_lease> assets,
                              std::shared_ptr<const prefab_placement_snapshot>& output) noexcept;
  static gneiss_result begin(runtime::application& application, gneiss_scene_instance scene,
                             std::shared_ptr<const prefab_placement_snapshot> snapshot,
                             bool removing, std::unique_ptr<prefab_placement>& output,
                             bool replacing = false) noexcept;
  ~prefab_placement();
  gneiss_result advance(bool& ready) noexcept;
  gneiss_result preview(std::size_t index, engine::scene::prefab_node_view& output) const noexcept;
  gneiss_result commit() noexcept;
  /** 仅接受同一场景的就绪替换事务；全部成功或全部保持原状。 */
  static gneiss_result commit_replacements(std::span<prefab_placement* const> entries) noexcept;
  bool cleanup_step(std::size_t budget = 64U) noexcept;
  std::size_t node_count() const noexcept;
  scene_node_id root() const noexcept;

private:
  gneiss_result create_next_node();
  prefab_placement(runtime::application& application,
                   std::shared_ptr<const prefab_placement_snapshot> snapshot,
                   gneiss_scene_instance scene, bool removing);
  runtime::application& application_;
  gneiss_scene_instance scene_{};
  std::shared_ptr<const prefab_placement_snapshot> snapshot_;
  engine::scene::prefab_candidate candidate_;
  std::map<std::string_view, scene_node_id, std::less<>> nodes_;
  std::size_t cursor_{};
  bool removing_{}, replacing_{}, retirement_ready_{}, ready_{}, committed_{};
  scene_node_id root_{};
};
}
