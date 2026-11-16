// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "commands/editor_command_history.hpp"
#include "documents/scene_document.hpp"
#include "documents/scene_document_snapshot.hpp"
#include <gneiss/engine/function/scene/scene_instance.hpp>
#include <functional>
#include <memory>
namespace gneiss::runtime {
class application;
}
namespace gneiss::editor {
class scene_edit_session;
/** 先预留历史空间，再执行一次领域编辑；撤销重做不重放 edit。 */
[[nodiscard]] gneiss::error execute_scene_edit(editor_command_history&, scene_edit_session&,
                                               std::string label,
                                               const std::function<gneiss::error()>& edit,
                                               std::string merge_key = {});
/** 捕获文档权威内容，不反向读取 World。 */
[[nodiscard]] gneiss::result<scene_document_snapshot>
capture_scene_document(runtime::application&, const scene_edit_session&);
[[nodiscard]] gneiss::error restore_scene_document(runtime::application&, scene_edit_session&,
                                                   const scene_document_snapshot&);
/** 一次文档内存变更；所属线程 begin/advance，不驱动 I/O，不写来源文件。
 * application、document、history 须覆盖操作及历史命令寿命；取消或析构释放候选资源引用。
 */
class scene_document_edit final {
public:
  scene_document_edit() = default;
  ~scene_document_edit() noexcept { cancel(); }
  scene_document_edit(const scene_document_edit&) = delete;
  scene_document_edit& operator=(const scene_document_edit&) = delete;
  gneiss::error begin(runtime::application&, scene_edit_session&,
                      std::span<const author_document_change>, std::string label,
                      std::function<void(bool undo)> selection = {});
  /** not_ready 表示等待；终态重复调用不重复提交。 */
  gneiss::error advance(runtime::application&, scene_edit_session&, editor_command_history&);
  void cancel() noexcept;

private:
  gneiss::error finish(gneiss::error);
  std::shared_ptr<scene_document_snapshot> before_, after_;
  scene_instance scene_{};
  runtime::application* application_{};
  scene_edit_session* document_{};
  std::string label_;
  std::function<void(bool)> selection_;
  gneiss::error status_{gneiss::error::invalid_state};
};
}
