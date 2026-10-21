// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/scene/scene_query.hpp"
#include "engine/resource/types/prefab_description.hpp"
#include <memory>
namespace gneiss::engine::scene {
class prefab_runtime_instance;
/** 未提交候选或已退休实例的独占事务；限 World 所属线程，须早于 World 释放。
 * 移动转移全部清理责任；析构兜底清理，交互路径应使用 cleanup_step 分步释放。 */
class GNEISS_API prefab_candidate final {
public:
  prefab_candidate() noexcept;
  ~prefab_candidate() noexcept;
  prefab_candidate(prefab_candidate&&) noexcept;
  prefab_candidate& operator=(prefab_candidate&&) noexcept;
  prefab_candidate(const prefab_candidate&) = delete;
  prefab_candidate& operator=(const prefab_candidate&) = delete;
  [[nodiscard]] bool is_valid() const noexcept;
  [[nodiscard]] scene_node_id root() const noexcept;
  [[nodiscard]] gneiss_result create_node(std::size_t index, scene_node_id parent,
                                          engine::render::mesh_asset_lease mesh,
                                          engine::render::material_asset_lease material,
                                          scene_node_id& output);
  /** 返回的文本借用到候选修改、移动、提交或清理；失败清空 output。 */
  [[nodiscard]] gneiss_result preview(std::size_t index, prefab_node_view& output) const noexcept;
  [[nodiscard]] bool cleanup_step(std::size_t budget) noexcept;

private:
  friend class scene_instance;
  gneiss::engine::resource::prefab_instance_description author_;
  std::unique_ptr<prefab_runtime_instance> runtime_;
};
}
