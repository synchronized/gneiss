// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "commands/editor_asset_edit.hpp"
#include "commands/editor_prefab_edit.hpp"
#include "documents/scene_document.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"

namespace gneiss::editor {
/** 单个文档的编辑所有者；全部调用限宿主线程。
 * 不拥有 Application 或执行器。销毁前须取消并排空编辑，再 close；析构不推进任务。
 * 场景加载与保存提示由调用方处理，不能在 active 时切换文档。
 */
class document_editing final {
public:
  document_editing() = default;
  document_editing(const document_editing&) = delete;
  document_editing& operator=(const document_editing&) = delete;
  scene_document& document() noexcept { return document_; }
  editor_command_history& history() noexcept { return history_; }
  editor_asset_edit& assets() noexcept { return assets_; }
  editor_prefab_edit& prefabs() noexcept { return prefabs_; }
  [[nodiscard]] bool active() const noexcept;
  void cancel(runtime::application& application);
  /** 只用于退出清理；调用者驱动协作执行器，不能与正常 UI 推进重复调用。 */
  void advance_cleanup(runtime::application& application);
  /** 有在途操作时不修改任何状态；成功后历史先于文档释放，可重复调用。 */
  [[nodiscard]] gneiss::engine::result close() noexcept;
  /** 先准备空场景；失败保留文档与历史，成功先释放旧历史再替换文档。 */
  [[nodiscard]] gneiss::engine::result create_empty(runtime::application& application,
                                                    gneiss::engine::rid world);

private:
  scene_document document_;
  editor_command_history history_;
  editor_asset_edit assets_;
  editor_prefab_edit prefabs_;
};
} // namespace gneiss::editor
