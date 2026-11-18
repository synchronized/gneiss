// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_document.hpp"
#include "documents/commands/editor_prefab_edit.hpp"
namespace gneiss::test_support {
inline gneiss::error drive_prefab_edit(runtime::application& app, editor::editor_prefab_edit& edit,
                                       gneiss::tasks::task_scheduler& tasks) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (edit.active()) {
    if (tasks.mode() == gneiss::tasks::execution_mode::cooperative)
      (void)tasks.run_ready();
    if (const auto frame = app.run(1); frame.failed())
      return frame;
    edit.advance();
    if (std::chrono::steady_clock::now() >= deadline)
      return gneiss::error::internal;
    std::this_thread::yield();
  }
  return edit.operation();
}
inline gneiss::error finish_history(editor::editor_command_history& history, gneiss::error status) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (history.is_pending()) {
    status = history.advance_pending();
    if (std::chrono::steady_clock::now() >= deadline)
      return gneiss::error::internal;
  }
  return status;
}
inline gneiss::error create_prefab_document(runtime::application& app,
                                            editor::scene_edit_session& doc, std::string_view name,
                                            std::string_view uri, entity_id parent, entity_id& root,
                                            gneiss::tasks::task_scheduler* tasks = nullptr) {
  root = {};
  if (!tasks)
    tasks = &application_tasks();
  editor::editor_prefab_edit edit;
  editor::editor_command_history history;
  auto status = edit.request(app, *tasks, doc, history, name, uri, parent);
  if (status.failed())
    return status;
  status = drive_prefab_edit(app, edit, *tasks);
  if (status.ok() && doc.selected_prefab_node())
    root = doc.selected_prefab_node()->node;
  return status;
}
inline gneiss::error refresh_prefab_document(runtime::application& app,
                                             editor::scene_edit_session& doc,
                                             editor::editor_command_history& history,
                                             entity_id root) {
  editor::editor_prefab_edit edit;
  auto status = edit.request_refresh(app, application_tasks(), doc, history, root);
  return status.failed() ? status : drive_prefab_edit(app, edit, application_tasks());
}
}
