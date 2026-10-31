// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "engine/function/scene/scene_types.hpp"
#include <map>

namespace gneiss::runtime {
class application;
}
namespace gneiss::runtime {
class GNEISS_API prefab_placement final {
public:
  /** 同步捕获整棵子树及资源引用，不读取文件。 */
  static gneiss::engine::error
  capture(runtime::application& application, gneiss::engine::guid uuid,
          gneiss::scene_instance_id scene,
          std::shared_ptr<engine::scene_subtree_snapshot>& snapshot) noexcept;
  static gneiss::engine::error
  freeze(runtime::application& application, gneiss::engine::prepared_prefab_description prepared,
         gneiss::engine::object_description author,
         std::vector<gneiss::engine::render_resource_ref> assets,
         std::shared_ptr<const engine::scene_subtree_snapshot>& output) noexcept;
  /** 同步创建或删除；替换不保留旧运行实例，output 失败置空。 */
  static gneiss::engine::error apply(application& application, scene_instance_id scene,
                                     const engine::scene_subtree_snapshot& snapshot, bool removing,
                                     bool replacing, scene_node_id& output) noexcept;
};
}
