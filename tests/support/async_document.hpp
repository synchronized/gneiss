// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_application.hpp"
#include "documents/scene_document.hpp"
namespace gneiss::test_support {
inline engine::error open_document(editor::scene_document& document, runtime::application& app,
                                   engine::world* world, std::string_view uri,
                                   engine::task_scheduler* tasks = nullptr) {
  if (world != app.native_world())
    return engine::error::invalid_argument;
  scene_instance_id instance;
  auto status = load_scene(app, uri, &instance, tasks);
  if (status.failed())
    return status;
  status = document.adopt(app, world, instance, uri);
  if (status.failed())
    (void)app.scenes()->unload(instance);
  return status;
}
}
