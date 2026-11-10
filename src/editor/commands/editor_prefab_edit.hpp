// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "commands/editor_command_history.hpp"
#include "documents/scene_edit_session.hpp"
#include "engine/core/error.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include <memory>
namespace gneiss::editor {
enum class prefab_edit_phase { preparing, assets, constructing, cleanup, finished };

/** 所属线程推进；宿主在销毁 Application、文档、历史或执行器前必须取消并排空。 */
class editor_prefab_edit final {
public:
  editor_prefab_edit();
  ~editor_prefab_edit();
  gneiss::engine::error request(runtime::application& application,
                                gneiss::engine::task_executor& executor,
                                scene_edit_session& session, editor_command_history& history,
                                std::string_view name, std::string_view uri, entity_id parent);
  gneiss::engine::error request_remove(runtime::application& application,
                                       scene_edit_session& session, editor_command_history& history,
                                       entity_id root);
  gneiss::engine::error request_refresh(runtime::application& application,
                                        gneiss::engine::task_executor& executor,
                                        scene_edit_session& session,
                                        editor_command_history& history, entity_id root);
  void advance();
  void cancel();
  bool active() const noexcept;
  bool visible() const noexcept;
  bool can_cancel() const noexcept;
  prefab_edit_phase phase() const noexcept;
  gneiss::engine::error operation() const noexcept;
  void dismiss() noexcept;

private:
  struct state;
  std::unique_ptr<state> state_;
};
}
