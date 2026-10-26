// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "commands/editor_command_history.hpp"
#include "documents/scene_document.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/function/scene/scene_instances.hpp"

namespace gneiss::editor {
/** 提交期间借用的资源地址；请求接收后自行复制 URI。 */
struct mesh_asset_reference final {
  std::string_view mesh_uri;
  std::string_view material_uri;
};

/** 所属线程的一次模态资产替换；命令租约须在 Application 销毁前释放。 */
class editor_asset_edit final {
public:
  gneiss::engine::result request_add(runtime::application& application, scene_document& session,
                                     editor_command_history& history, std::string_view name,
                                     mesh_asset_reference assets);
  gneiss::engine::result request(runtime::application& application, scene_document& session,
                                 editor_command_history& history, std::string_view uuid,
                                 mesh_asset_reference assets);
  /** 移除组件并把资源强引用保存在历史中；撤销不加载文件。 */
  gneiss::engine::result request_remove(runtime::application& application, scene_document& session,
                                        editor_command_history& history, std::string_view uuid);
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
  gneiss::engine::result operation() const noexcept { return operation_; }
  const gneiss::engine::asset_load_progress& progress() const noexcept { return progress_; }

private:
  gneiss::engine::result request_impl(runtime::application& application, scene_document& session,
                                      editor_command_history& history, std::string_view target,
                                      mesh_asset_reference assets, bool creating);
  gneiss::engine::result commit_add(runtime::application& application, scene_document& session,
                                    editor_command_history& history);
  scene_node_snapshot snapshot_;
  bool creating_{};
  gneiss::engine::result commit(runtime::application& application, scene_document& session,
                                editor_command_history& history);
  gneiss::engine::scene_mesh_binding previous_, next_;
  gneiss::scene_instance_id scene_{};
  scene_node_id node_;
  std::string mesh_, material_;
  std::uint64_t revision_{}, request_{};
  gneiss::engine::asset_load_progress progress_;
  gneiss::engine::result operation_{gneiss::engine::result::success};
  bool active_{}, visible_{}, cancelled_{};
};
}
