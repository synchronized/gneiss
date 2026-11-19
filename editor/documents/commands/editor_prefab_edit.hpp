// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/commands/editor_command_history.hpp"
#include "documents/scene_edit_session.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/tasks/scheduler.hpp>
#include <memory>
namespace gneiss::editor {
enum class prefab_edit_phase { preparing, assets, constructing, cleanup, finished };

/** 所属线程推进；宿主在销毁 Application、文档、历史或执行器前必须取消并排空。 */
class editor_prefab_edit final {
public:
  editor_prefab_edit();
  ~editor_prefab_edit();
  gneiss::error request(runtime::application& application, gneiss::tasks::executor& executor,
                        scene_edit_session& session, editor_command_history& history,
                        std::string_view name, std::string_view uri, entity_id parent);
  gneiss::error request_remove(runtime::application& application, scene_edit_session& session,
                               editor_command_history& history, entity_id root);
  gneiss::error request_refresh(runtime::application& application,
                                gneiss::tasks::executor& executor, scene_edit_session& session,
                                editor_command_history& history, entity_id root);
  void advance();
  void cancel();
  bool active() const noexcept;
  bool visible() const noexcept;
  bool can_cancel() const noexcept;
  prefab_edit_phase phase() const noexcept;
  gneiss::error operation() const noexcept;
  void dismiss() noexcept;

private:
  struct state;
  std::unique_ptr<state> state_;
};
}
