// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../../tests/support/c_owner.hpp"
#include "editor_scene_loader.hpp"
#include "runtime/application_asset_reload_internal.hpp"
#include "runtime/application_registry.hpp"

#include <gneiss/engine/application.h>
#include <gneiss/engine/input.hpp>
#include <gneiss/engine/log.hpp>
#include <gneiss/engine/render.hpp>
#include <gneiss/engine/scene.hpp>
#include <gneiss/engine/world.h>

#include <chrono>
#include <cstdio>
#include <source_location>
#include <stdexcept>
#include <thread>

using namespace gneiss;

namespace {
using namespace gneiss;
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("编辑器异步加载验证失败：" + std::to_string(where.line()));
}

void run(tasks::execution_mode mode) {
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  gneiss::test_support::application_owner app;
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  const std::string root = GNEISS_EDITOR_TEST_ASSET_ROOT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(gneiss::result{gneiss_application_create(&desc, app.put())} == result::success);
  check(application_internal::attach_task_executor(app.get(), scheduler) == GNEISS_SUCCESS);
  editor::editor_session session;
  editor::editor_camera camera;
  editor::editor_command_history history;
  editor::editor_scene_loader loader;
  gneiss_world world{};
  check(gneiss::result{gneiss_application_get_world(app.get(), &world)} == result::success);
  check(session.create_empty(*gneiss::application_internal::find_application(app.get()), world) ==
        result::success);
  check(camera.initialize() == result::success);
  const auto original = world;
  constexpr std::string_view uri = "asset://scenes/triangle.scene.json";
  auto tick = [&] {
    if (mode == tasks::execution_mode::cooperative)
      (void)scheduler.run_ready();
    check(gneiss::result{gneiss_application_run(app.get(), 1U)} == result::success);
    bool changed{};
    check(loader.advance(*gneiss::application_internal::find_application(app.get()), session, world,
                         camera, history, changed) == result::success);
    std::this_thread::yield();
  };
  auto drain = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
    while (loader.active()) {
      check(std::chrono::steady_clock::now() < deadline);
      tick();
    }
  };
  check(loader.request(*gneiss::application_internal::find_application(app.get()), uri) ==
        result::success);
  check(loader.active() && loader.visible() && world == original);
  check(loader.request(*gneiss::application_internal::find_application(app.get()), uri) ==
        result::not_ready);
  drain();
  check(!loader.visible() && loader.operation() == result::success && world != original);
  check(session.is_open() && !session.is_dirty() && session.uri() == uri);
  check(session.nodes().size() == 2U && camera.is_valid());
  const auto current = world;
  const auto node = session.nodes()[1].node;
  check(session.select(node) == result::success);
  check(session.rename_node(node, "Edited") == result::success);
  auto command =
      std::make_unique<editor::functional_editor_command>(editor::functional_editor_command::desc{
          .label = "Rename",
          .undo = [&] { return session.rename_node(node, "Original"); },
          .redo = [&] { return session.rename_node(node, "Edited"); },
          .merge_key = {}});
  check(history.record(std::move(command)) == result::success);
  check(history.undo() == result::success && history.redo() == result::success);
  check(loader.request(*gneiss::application_internal::find_application(app.get()),
                       "asset://missing.scene.json") == result::success);
  drain();
  check(loader.visible() && loader.operation() != result::success);
  check(world == current && session.selection() == node && session.is_dirty());
  check(session.selected_node()->display_name == "Edited" && history.can_undo());
  loader.dismiss();
  check(!loader.visible());
  check(loader.request(*gneiss::application_internal::find_application(app.get()), uri) ==
        result::success);
  tick();
  check(loader.cancel(*gneiss::application_internal::find_application(app.get())) ==
        result::success);
  drain();
  check(world == current && session.selection() == node && history.can_undo());
  check(loader.progress().phase == application_internal::scene_load_phase::cancelled);
  check(loader.request(*gneiss::application_internal::find_application(app.get()), uri) ==
        result::success);
  drain();
  check(world != current && !session.selection().is_valid() && !history.can_undo());
  check(camera.is_valid() && session.nodes().size() == 2U);
  tick();
  std::uint64_t count{};
  check(gneiss_world_entity_count(current, &count) == GNEISS_ERROR_INVALID_HANDLE);
  check(loader.request(*gneiss::application_internal::find_application(app.get()),
                       "asset://scenes/prefab.scene.json") == result::success);
  drain();
  check(loader.operation() == result::success && session.nodes().size() == 1U &&
        session.prefab_nodes().size() == 2U && camera.is_valid());
  // 无效场景的 C 查询拒绝，不影响活动场景。
  check(gneiss_scene_instance_get_node_count(app.get(), GNEISS_NULL_SCENE_INSTANCE, &count) ==
        GNEISS_ERROR_INVALID_HANDLE);
  camera.shutdown();
  session.close();
  // 在途任务随 Application 关闭；执行器必须最后销毁。
  check(loader.request(*gneiss::application_internal::find_application(app.get()), uri) ==
        result::success);
  tick();
}
} // namespace

int main() try {
  run(gneiss::tasks::execution_mode::thread_pool);
  run(gneiss::tasks::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
