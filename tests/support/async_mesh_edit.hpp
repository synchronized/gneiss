// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_document.hpp"
#include "commands/editor_asset_edit.hpp"
namespace gneiss::test_support {
inline engine::result
create_mesh_document(runtime::application& app, editor::scene_document& doc, std::string_view name,
                     std::string_view mesh, std::string_view material, scene_node_id& output,
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
    app.advance_scene_load();
    edit.advance(app, doc, history);
    if (std::chrono::steady_clock::now() >= deadline)
      return engine::result::internal;
    std::this_thread::yield();
  }
  status = edit.operation();
  if (status.ok() && doc.selected_node())
    output = doc.selected_node()->node;
  return status;
}
}
