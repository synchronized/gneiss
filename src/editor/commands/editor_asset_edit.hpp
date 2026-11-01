// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "commands/author_property_edit.hpp"
#include "documents/scene_document.hpp"
#include "documents/scene_document_snapshot.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/resource/render_resource.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "runtime/loading/resource_load_types.hpp"

namespace gneiss::editor {
/** 提交期间借用的资源地址；请求接收后自行复制 URI。 */
struct mesh_asset_reference final {
  std::string_view mesh_uri;
  std::string_view material_uri;
};

/** 所属线程的一次模态资产替换；命令租约须在 Application 销毁前释放。 */
class editor_asset_edit final {
public:
  gneiss::engine::error request_add(runtime::application& application, scene_document& session,
                                    editor_command_history& history, std::string_view name,
                                    mesh_asset_reference assets);
  gneiss::engine::error request(runtime::application& application, scene_document& session,
                                editor_command_history& history, gneiss::engine::guid uuid,
                                mesh_asset_reference assets);
  /** 普通节点及 Prefab 来源节点共用异步入口；目标按稳定作者身份重新定位。 */
  engine::error request(runtime::application&, scene_document&, editor_command_history&,
                        const component_edit_target&, mesh_asset_reference);
  /** 异步准备来源网格资源后清除实例组件覆盖。 */
  engine::error request_restore_source(runtime::application&, scene_document&,
                                       editor_command_history&, const component_edit_target&);
  /** 反射 URI 属性编辑，先校验描述，再复用资源替换请求。 */
  engine::error request_property(runtime::application&, scene_document&, editor_command_history&,
                                 const component_edit_target&, const property_edit&);
  /** 移除组件并把资源强引用保存在历史中；撤销不加载文件。 */
  gneiss::engine::error request_remove(scene_document& session, editor_command_history& history,
                                       gneiss::engine::guid uuid);
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
  gneiss::engine::error operation() const noexcept { return operation_; }
  const gneiss::runtime::asset_load_progress& progress() const noexcept { return progress_; }

private:
  void finish(runtime::application& application);
  void accept_assets(std::span<const engine::render_resource_ref> assets);
  gneiss::engine::error request_impl(runtime::application& application, scene_document& session,
                                     editor_command_history& history,
                                     const component_edit_target& target, std::string_view name,
                                     mesh_asset_reference assets, bool creating);
  gneiss::engine::error commit_add(scene_document& session, editor_command_history& history);
  mesh_node_creation snapshot_;
  bool creating_{};
  std::shared_ptr<scene_document_edit> restore_;
  std::optional<engine::prefab_component_state> prefab_before_{};
  gneiss::engine::error commit(runtime::application& application, scene_document& session,
                               editor_command_history& history);
  gneiss::engine::scene_mesh_binding previous_, next_;
  gneiss::scene_instance_id scene_{};
  scene_node_id node_;
  component_edit_target target_;
  std::string mesh_, material_;
  std::uint64_t revision_{}, request_{};
  gneiss::runtime::asset_load_progress progress_;
  gneiss::engine::error operation_{gneiss::engine::error::success};
  bool active_{}, visible_{}, cancelled_{};
};
}
