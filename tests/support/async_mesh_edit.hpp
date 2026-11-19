// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_document.hpp"
#include "documents/commands/editor_asset_edit.hpp"
namespace gneiss::test_support {
inline gneiss::error
create_mesh_document(runtime::application& app, editor::scene_edit_session& doc,
                     std::string_view name, std::string_view mesh, std::string_view material,
                     entity_id& output,
                     editor::editor_command_history* retained_history = nullptr) {
  output = {};
  editor::editor_command_history temporary_history;
  auto& history = retained_history ? *retained_history : temporary_history;
  editor::editor_asset_edit edit;
  auto status = edit.request_add(app, doc, history, name, {mesh, material});
  if (status.failed())
    return status;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (edit.active()) {
    const auto frame = app.step();
    if (!frame)
      return frame.error();
    if (*frame == app::lifecycle::stopping || *frame == app::lifecycle::stopped)
      return error::cancelled;
    edit.advance(app, doc, history);
    if (std::chrono::steady_clock::now() >= deadline)
      return gneiss::error::internal;
    std::this_thread::yield();
  }
  status = edit.operation();
  if (status.ok() && doc.selected_node())
    output = doc.selected_node()->node;
  return status;
}
}
