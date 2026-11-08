// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "commands/editor_command_history.hpp"
#include "documents/scene_document.hpp"
#include "documents/scene_document_snapshot.hpp"
#include "engine/function/scene/scene_types.hpp"
#include <functional>
#include <memory>
namespace gneiss::runtime {
class application;
}
namespace gneiss::editor {
class scene_edit_session;
/** 记录已经完成的文档编辑，历史持有局部对象差异及轻量版本标识，不保留整份前后内容。 */
[[nodiscard]] engine::error record_scene_edit(editor_command_history&, scene_edit_session&,
                                              const scene_document::version& before,
                                              std::string label,
                                              std::string merge_key = {}) noexcept;
/** 先预留历史空间，再执行一次领域编辑；撤销重做不重放 edit。 */
[[nodiscard]] engine::error execute_scene_edit(editor_command_history&, scene_edit_session&,
                                               std::string label,
                                               const std::function<engine::error()>& edit,
                                               std::string merge_key = {}) noexcept;
/** 捕获文档权威内容，不反向读取 World。 */
[[nodiscard]] engine::result<scene_document_snapshot>
capture_scene_document(runtime::application&, const scene_edit_session&) noexcept;
[[nodiscard]] engine::error restore_scene_document(runtime::application&, scene_edit_session&,
                                                   const scene_document_snapshot&) noexcept;
/** 一次文档内存变更；所属线程 begin/advance，不驱动 I/O，不写来源文件。
 * application、document、history 须覆盖操作及历史命令寿命；取消或析构释放候选资源引用。
 */
class scene_document_edit final {
public:
  scene_document_edit() = default;
  ~scene_document_edit() noexcept { cancel(); }
  scene_document_edit(const scene_document_edit&) = delete;
  scene_document_edit& operator=(const scene_document_edit&) = delete;
  engine::error begin(runtime::application&, scene_edit_session&,
                      std::span<const author_document_change>, std::string label,
                      std::function<void(bool undo)> selection = {}) noexcept;
  /** not_ready 表示等待；终态重复调用不重复提交。 */
  engine::error advance(runtime::application&, scene_edit_session&,
                        editor_command_history&) noexcept;
  void cancel() noexcept;

private:
  engine::error finish(engine::error) noexcept;
  std::shared_ptr<scene_document_snapshot> before_, after_;
  scene_instance_id scene_{};
  runtime::application* application_{};
  scene_edit_session* document_{};
  std::string label_;
  std::function<void(bool)> selection_;
  engine::error status_{engine::error::invalid_state};
};
}
