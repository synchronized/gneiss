// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "scene/runtime_scene_loader.hpp"
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/function/scene/rendering/world_render_binding.hpp>
#include <gneiss/engine/function/scene/world.hpp>
namespace gneiss::runtime {
/** 运行空间所有者；借用 Application，所属线程使用。
 * 服务就绪后 initialize；停止外部检查器等借用后 close，再关闭 Application。
 * 当前加载协议适配尚在宿主，loading 是迁移期间的显式借用入口。 */
class session final {
public:
  explicit session(app::application& application) : application_(application) {}
  session(const session&) = delete;
  session& operator=(const session&) = delete;
  result<void> initialize();
  gneiss::world& world() const { return *world_; }
  host::runtime_scene_loader& loading() { return *loader_; }
  result<void> synchronize();
  result<void> append_views(render::frame_request&) const;
  /** 解除加载回调并放弃请求；不销毁 World，外部借用仍可清理。 */
  void stop_loading();
  void close();

private:
  app::application& application_;
  world_owner world_;
  std::unique_ptr<world_render_binding> binding_;
  std::optional<host::runtime_scene_loader> loader_;
};
}
