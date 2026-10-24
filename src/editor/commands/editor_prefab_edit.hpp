// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "commands/editor_command_history.hpp"
#include "documents/scene_document.hpp"
#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include <memory>
namespace gneiss::editor {
enum class prefab_edit_phase { preparing, assets, constructing, cleanup, finished };

/** 所属线程推进；宿主在销毁 Application、文档、历史或执行器前必须取消并排空。 */
class editor_prefab_edit final {
public:
  editor_prefab_edit();
  ~editor_prefab_edit();
  gneiss::engine::result request(runtime::application& application,
                                 gneiss::engine::task_executor& executor, scene_document& session,
                                 editor_command_history& history, std::string_view name,
                                 std::string_view uri, scene_node_id parent);
  gneiss::engine::result request_remove(runtime::application& application, scene_document& session,
                                        editor_command_history& history, scene_node_id root);
  gneiss::engine::result request_refresh(runtime::application& application,
                                         gneiss::engine::task_executor& executor,
                                         scene_document& session, editor_command_history& history,
                                         scene_node_id root);
  void advance();
  void cancel();
  bool active() const noexcept;
  bool visible() const noexcept;
  bool can_cancel() const noexcept;
  prefab_edit_phase phase() const noexcept;
  gneiss::engine::result operation() const noexcept;
  void dismiss() noexcept;

private:
  struct state;
  std::unique_ptr<state> state_;
};
}
