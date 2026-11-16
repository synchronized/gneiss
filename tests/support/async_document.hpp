// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_application.hpp"
#include "documents/scene_edit_session.hpp"
#include <gneiss/engine/function/scene/instantiation.hpp>
#include "scene_access.hpp"
#include "scene_instantiation.hpp"
namespace gneiss::test_support {
inline gneiss::error open_document(editor::scene_edit_session& document, runtime::application& app,
                                   gneiss::world* world, std::string_view uri,
                                   gneiss::tasks::task_scheduler* tasks = nullptr) {
  if (world != app.native_world())
    return gneiss::error::invalid_argument;
  gneiss::scene_content prepared;
  gneiss::ref<const gneiss::scene> source;
  gneiss::scene_resource_bindings bindings;
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
  status = document.adopt(app, instance, *initial, created->objects);
  if (status.failed())
    (void)gneiss::test_support::scene_access{app}->unload(instance);
  return status;
}
}
