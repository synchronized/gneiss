// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/editor_scene_loader.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"

#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"

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

void run(gneiss::engine::execution_mode mode) {
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<runtime::application> app;
  runtime::application_configuration desc;
  const std::string root = GNEISS_EDITOR_TEST_ASSET_ROOT;
  desc.asset_root = root;
  check(runtime::application::create(desc, app) == gneiss::engine::result::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::success);
  editor::document_editing editing;
  auto& session = editing.document();
  editor::editor_camera camera;
  auto& history = editing.history();
  editor::editor_scene_loader loader;
  auto world = app->native_world();
  check(session.create_empty(*app, world) == gneiss::engine::result::success);
  check(camera.initialize() == gneiss::engine::result::success);
  const auto original = world;
  constexpr std::string_view uri = "asset://scenes/triangle.scene.json";
  auto tick = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative)
      (void)scheduler.run_ready();
    check(gneiss::engine::result{app->run(1U)} == gneiss::engine::result::success);
    bool changed{};
    check(loader.advance(*app, editing, world, camera, changed) == gneiss::engine::result::success);
    std::this_thread::yield();
  };
  auto drain = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
    while (loader.active()) {
      check(std::chrono::steady_clock::now() < deadline);
      tick();
    }
  };
  check(loader.request(*app, editing, uri) == gneiss::engine::result::success);
  check(loader.active() && loader.visible() && world == original);
  check(loader.request(*app, editing, uri) == gneiss::engine::result::not_ready);
  drain();
  check(!loader.visible() && loader.operation() == gneiss::engine::result::success &&
        world != original);
  check(session.is_open() && !session.is_dirty() && session.uri() == uri);
  check(session.nodes().size() == 2U && camera.is_valid());
  const auto current = world;
  const auto node = session.nodes()[1].node;
  check(session.select(node) == gneiss::engine::result::success);
  check(session.rename_node(node, "Edited") == gneiss::engine::result::success);
  auto command =
      std::make_unique<editor::functional_editor_command>(editor::functional_editor_command::desc{
          .label = "Rename",
          .undo = [&] { return session.rename_node(node, "Original"); },
          .redo = [&] { return session.rename_node(node, "Edited"); },
          .merge_key = {}});
  check(history.record(std::move(command)) == gneiss::engine::result::success);
  check(history.undo() == gneiss::engine::result::success &&
        history.redo() == gneiss::engine::result::success);
  check(loader.request(*app, editing, "asset://missing.scene.json") ==
        gneiss::engine::result::success);
  drain();
  check(loader.visible() && loader.operation() != gneiss::engine::result::success);
  check(world == current && session.selection() == node && session.is_dirty());
  check(session.selected_node()->display_name == "Edited" && history.can_undo());
  loader.dismiss();
  check(!loader.visible());
  check(loader.request(*app, editing, uri) == gneiss::engine::result::success);
  tick();
  check(loader.cancel(*app) == gneiss::engine::result::success);
  drain();
  check(world == current && session.selection() == node && history.can_undo());
  check(loader.progress().phase == runtime::scene_load_phase::cancelled);
  check(loader.request(*app, editing, uri) == gneiss::engine::result::success);
  drain();
  check(world != current && !session.selection().is_valid() && !history.can_undo());
  check(camera.is_valid() && session.nodes().size() == 2U);
  tick();
  std::uint64_t count{};
  check(gneiss::engine::result{current->entity_count(count)} ==
        gneiss::engine::result::invalid_handle);
  check(loader.request(*app, editing, "asset://scenes/prefab.scene.json") ==
        gneiss::engine::result::success);
  drain();
  check(loader.operation() == gneiss::engine::result::success && session.nodes().size() == 1U &&
        session.prefab_nodes().size() == 2U && camera.is_valid());
  // 无效场景的原生查询拒绝，不影响活动场景。
  check(gneiss::engine::result{app->scenes()->get_node_count(
            gneiss::scene_instance_id{}, &count)} == gneiss::engine::result::invalid_handle);
  camera.shutdown();
  session.close();
  // 在途任务随 Application 关闭；执行器必须最后销毁。
  check(loader.request(*app, editing, uri) == gneiss::engine::result::success);
  tick();
}
} // namespace

int main() try {
  run(gneiss::engine::execution_mode::thread_pool);
  run(gneiss::engine::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
