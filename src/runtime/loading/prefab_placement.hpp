// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"
#include <map>

namespace gneiss::runtime {
class application;
}
namespace gneiss::runtime {
/** 历史持有的不可变来源与实际资产租约，不保存隐藏实例。 */
struct prefab_placement_snapshot {
  gneiss::engine::prefab_instance_description author;
  gneiss::engine::prefab_asset_ref prefab;
  std::vector<std::size_t> order;
  std::vector<gneiss::engine::scene_mesh_binding> bindings;
  std::map<std::string, gneiss::engine::render_asset_ref, std::less<>> assets;
};
class GNEISS_API prefab_placement final {
public:
  /** 每次捕获最多 64 个节点；完整之前不得用于构造或删除提交。 */
  static gneiss::engine::result capture_step(runtime::application& application,
                                             std::string_view uuid, gneiss::engine::rid scene,
                                             std::shared_ptr<prefab_placement_snapshot>& snapshot,
                                             bool& complete) noexcept;
  static gneiss::engine::result
  freeze(runtime::application& application, gneiss::engine::prepared_prefab_description prepared,
         gneiss::engine::prefab_instance_description author,
         std::vector<gneiss::engine::render_asset_ref> assets,
         std::shared_ptr<const prefab_placement_snapshot>& output) noexcept;
  static gneiss::engine::result begin(runtime::application& application, gneiss::engine::rid scene,
                                      std::shared_ptr<const prefab_placement_snapshot> snapshot,
                                      bool removing, std::unique_ptr<prefab_placement>& output,
                                      bool replacing = false) noexcept;
  ~prefab_placement();
  gneiss::engine::result advance(bool& ready) noexcept;
  gneiss::engine::result preview(std::size_t index,
                                 gneiss::engine::prefab_node_view& output) const noexcept;
  gneiss::engine::result commit() noexcept;
  /** 仅接受同一场景的就绪替换事务；全部成功或全部保持原状。 */
  static gneiss::engine::result
  commit_replacements(std::span<prefab_placement* const> entries) noexcept;
  bool cleanup_step(std::size_t budget = 64U) noexcept;
  std::size_t node_count() const noexcept;
  scene_node_id root() const noexcept;

private:
  gneiss::engine::result create_next_node();
  prefab_placement(runtime::application& application,
                   std::shared_ptr<const prefab_placement_snapshot> snapshot,
                   gneiss::engine::rid scene, bool removing);
  runtime::application& application_;
  gneiss::engine::rid scene_{};
  std::shared_ptr<const prefab_placement_snapshot> snapshot_;
  gneiss::engine::prefab_candidate candidate_;
  std::map<std::string_view, scene_node_id, std::less<>> nodes_;
  std::size_t cursor_{};
  bool removing_{}, replacing_{}, retirement_ready_{}, ready_{}, committed_{};
  scene_node_id root_{};
};
}
