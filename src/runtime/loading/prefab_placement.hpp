// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
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
  static gneiss::engine::error capture_step(runtime::application& application,
                                            std::string_view uuid, gneiss::scene_instance_id scene,
                                            std::shared_ptr<prefab_placement_snapshot>& snapshot,
                                            bool& complete) noexcept;
  static gneiss::engine::error
  freeze(runtime::application& application, gneiss::engine::prepared_prefab_description prepared,
         gneiss::engine::prefab_instance_description author,
         std::vector<gneiss::engine::render_asset_ref> assets,
         std::shared_ptr<const prefab_placement_snapshot>& output) noexcept;
  /** 同步创建或删除；替换不保留旧运行实例，output 失败置空。 */
  static gneiss::engine::error apply(application& application, scene_instance_id scene,
                                     const prefab_placement_snapshot& snapshot, bool removing,
                                     bool replacing, scene_node_id& output) noexcept;
};
}
