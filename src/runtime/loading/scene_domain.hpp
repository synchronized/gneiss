// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/result.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world_service.hpp"

namespace gneiss::engine::resource {
class asset_manager;
}

namespace gneiss::runtime {

/** 一个完整场景域，拥有 World 和场景实例集合；可容纳多个 Scene。
 * 仅所属线程操作。实例集合借用的资源管理器与渲染加载器必须比域更晚销毁。
 * 候选域未激活前，Application 的查询仍访问旧域。 */
class scene_domain final {
public:
  scene_domain() = default;
  ~scene_domain() noexcept;
  scene_domain(const scene_domain&) = delete;
  scene_domain& operator=(const scene_domain&) = delete;
  /** 全部初始化成功后发布；失败保持空域，可重试。重复初始化拒绝且保持原状态。 */
  [[nodiscard]] gneiss::engine::core::result
  initialize(engine::resource::asset_manager& owner,
             engine::render::render_asset_loader& render) noexcept;
  [[nodiscard]] engine::world::world* native_world() const noexcept { return world_owner_.get(); }
  [[nodiscard]] gneiss_world world() const noexcept {
    return world_owner_ ? world_owner_.get_deleter().borrowed : GNEISS_NULL_WORLD;
  }
  /** 借用实例集合；初始化前为空，有效期不超过本域。 */
  [[nodiscard]] engine::scene::scene_instances* scenes() const noexcept { return scenes_.get(); }

private:
  // 按声明逆序析构：先释放场景及其资源引用，再撤销 World 的借用 RID。
  engine::world::world_owner world_owner_;
  std::unique_ptr<engine::scene::scene_instances> scenes_;
};

} // namespace gneiss::runtime
