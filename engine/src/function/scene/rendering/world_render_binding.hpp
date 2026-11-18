// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/events/signal.hpp>
#include <gneiss/engine/result.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/function/render/commands.hpp>
#include <gneiss/engine/function/scene/object_change.hpp>
#include <memory>
namespace gneiss {
class world;
namespace render {
class service;
}
/** World 到持久渲染场景的单向同步。只在两个服务的所属线程使用。
 * 借用 World 和 Render，二者须覆盖使用期；析构释放拥有的渲染场景。
 * 不改变 World 的所有权，不负责窗口、相机或帧提交。 */
class world_render_binding final {
public:
  [[nodiscard]] static GNEISS_API result<std::unique_ptr<world_render_binding>>
  create(world&, gneiss::render::service&);
  GNEISS_API ~world_render_binding();
  world_render_binding(const world_render_binding&) = delete;
  world_render_binding& operator=(const world_render_binding&) = delete;
  /** 借用场景 RID；由绑定销毁，调用方不得自行销毁或提交实例修改命令。 */
  [[nodiscard]] GNEISS_API rid scene() const noexcept;
  /** 创建时枚举已有网格对象；后续须由宿主先 flush_changes，再同步。失败保留待同步状态，可重试。
   * 缺失 mesh/material 的对象暂不创建实例，资源换绑通知后重新判断。 */
  [[nodiscard]] GNEISS_API result<void> synchronize();

private:
  world_render_binding(world&, render::service&);
  void on_changed(const object_changes&);
  world& source_;
  render::service& render_;
  rid scene_{};
  object_changes pending_;
  std::map<entity_id, render::instance_key> instances_;
  std::uint64_t last_key_{};
  events::scoped_connection subscription_;
};
}
