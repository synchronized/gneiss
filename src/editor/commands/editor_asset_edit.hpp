// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "commands/asset_scene_commands.hpp"
#include "commands/editor_command_history.hpp"
#include "documents/scene_document.hpp"
#include "engine/core/result.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/function/scene/scene_instances.hpp"

namespace gneiss::editor {
/** 所属线程的一次模态资产替换；命令租约须在 Application 销毁前释放。 */
class editor_asset_edit final {
public:
  gneiss::engine::core::result request_add(runtime::application& application,
                                           scene_document& session, editor_command_history& history,
                                           std::string_view name, mesh_asset_reference assets);
  gneiss::engine::core::result request(runtime::application& application, scene_document& session,
                                       editor_command_history& history, std::string_view uuid,
                                       mesh_asset_reference assets);
  void advance(runtime::application& application, scene_document& session,
               editor_command_history& history);
  void cancel(runtime::application& application);
  void dismiss() noexcept {
    if (!active_) {
      visible_ = false;
    }
  }
  bool active() const noexcept { return active_; }
  bool visible() const noexcept { return visible_; }
  gneiss::engine::core::result operation() const noexcept { return operation_; }
  const engine::render::asset_load_progress& progress() const noexcept { return progress_; }

private:
  gneiss::engine::core::result request_impl(runtime::application& application,
                                            scene_document& session,
                                            editor_command_history& history,
                                            std::string_view target, mesh_asset_reference assets,
                                            bool creating);
  gneiss::engine::core::result commit_add(runtime::application& application,
                                          scene_document& session, editor_command_history& history);
  scene_node_snapshot snapshot_;
  bool creating_{};
  gneiss::engine::core::result commit(runtime::application& application, scene_document& session,
                                      editor_command_history& history);
  engine::scene::scene_mesh_binding previous_, next_;
  gneiss_scene_instance scene_{};
  scene_node_id node_;
  std::string mesh_, material_;
  std::uint64_t revision_{}, request_{};
  engine::render::asset_load_progress progress_;
  gneiss::engine::core::result operation_{gneiss::engine::core::result::success};
  bool active_{}, visible_{}, cancelled_{};
};
}
