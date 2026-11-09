// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_application.hpp"
#include "documents/scene_edit_session.hpp"
#include "engine/function/scene/scene_instantiation.hpp"
namespace gneiss::test_support {
inline engine::error open_document(editor::scene_edit_session& document, runtime::application& app,
                                   engine::world* world, std::string_view uri,
                                   engine::task_scheduler* tasks = nullptr) {
  if (world != app.native_world())
    return engine::error::invalid_argument;
  engine::scene_instantiation_data prepared;
  engine::scene_resource_bindings bindings;
  auto status = prepare_scene(app, uri, prepared, bindings, tasks, false, true);
  if (status.failed())
    return status;
  auto initial = editor::make_scene_document_snapshot(std::string(uri), prepared);
  if (!initial)
    return initial.error();
  initial->assets = bindings;
  auto created =
      gneiss::engine::instantiate_scene(*app.native_world(), std::move(prepared), bindings);
  if (!created)
    return created.error();
  const auto instance = created->instance;
  status = document.adopt(app, world, instance, *initial, created->objects);
  if (status.failed())
    (void)app.scenes()->unload(instance);
  return status;
}
}
