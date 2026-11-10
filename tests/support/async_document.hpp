// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_application.hpp"
#include "documents/scene_edit_session.hpp"
#include "engine/function/scene/scene_instantiation.hpp"
#include "scene_access.hpp"
#include "scene_instantiation.hpp"
namespace gneiss::test_support {
inline engine::error open_document(editor::scene_edit_session& document, runtime::application& app,
                                   engine::world* world, std::string_view uri,
                                   engine::task_scheduler* tasks = nullptr) {
  if (world != app.native_world())
    return engine::error::invalid_argument;
  engine::scene_content prepared;
  engine::ref<const engine::scene> source;
  engine::scene_resource_bindings bindings;
  auto status = prepare_scene(app, uri, prepared, bindings, tasks, &source, true);
  if (status.failed())
    return status;
  auto initial = editor::make_scene_document_snapshot(std::string(uri), prepared, source);
  if (!initial)
    return initial.error();
  initial->assets = bindings;
  auto created = gneiss::test_support::scene_fixture::instantiate_scene(
      *app.native_world(), std::move(prepared), bindings);
  if (!created)
    return created.error();
  const auto instance = created->instance;
  status = document.adopt(app, world, instance, *initial, created->objects);
  if (status.failed())
    (void)gneiss::test_support::scene_access{app}->unload(instance);
  return status;
}
}
