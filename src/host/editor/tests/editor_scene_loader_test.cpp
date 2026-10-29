// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../../../tests/support/value_result.hpp"
#include "documents/editor_scene_loader.hpp"
#include "documents/scene_document_snapshot.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/core/rid.hpp"
#include "engine/resource/builtin_resource_registration.hpp"

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
  check(runtime::application::create(desc, app) == gneiss::engine::error::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::error::success);
  editor::document_editing editing;
  auto& session = editing.document();
  editor::editor_camera camera;
  auto& history = editing.history();
  editor::editor_scene_loader loader;
  auto world = app->native_world();
  check(session.create_empty(*app, world) == gneiss::engine::error::success);
  check(camera.initialize() == gneiss::engine::error::success);
  const auto original = world;
  constexpr std::string_view uri = "asset://scenes/triangle.scene.json";
  auto tick = [&] {
    if (mode == gneiss::engine::execution_mode::cooperative)
      (void)scheduler.run_ready();
    check(gneiss::engine::error{app->run(1U)} == gneiss::engine::error::success);
    bool changed{};
    check(loader.advance(*app, editing, world, camera, changed) == gneiss::engine::error::success);
    std::this_thread::yield();
  };
  auto drain = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
    while (loader.active()) {
      check(std::chrono::steady_clock::now() < deadline);
      tick();
    }
  };
  check(loader.request(*app, editing, uri) == gneiss::engine::error::success);
  check(loader.active() && loader.visible() && world == original);
  check(loader.request(*app, editing, uri) == gneiss::engine::error::not_ready);
  drain();
  check(!loader.visible() && loader.operation() == gneiss::engine::error::success &&
        world != original);
  check(session.is_open() && !session.is_dirty() && session.uri() == uri);
  check(session.nodes().size() == 2U && camera.is_valid());
  const auto current = world;
  const auto node = session.nodes()[1].node;
  check(session.select(node) == gneiss::engine::error::success);
  check(session.rename_node(node, "Edited") == gneiss::engine::error::success);
  auto command =
      std::make_unique<editor::functional_editor_command>(editor::functional_editor_command::desc{
          .label = "Rename",
          .undo = [&] { return session.rename_node(node, "Original"); },
          .redo = [&] { return session.rename_node(node, "Edited"); },
          .merge_key = {}});
  check(history.record(std::move(command)) == gneiss::engine::error::success);
  check(history.undo() == gneiss::engine::error::success &&
        history.redo() == gneiss::engine::error::success);
  check(loader.request(*app, editing, "asset://missing.scene.json") ==
        gneiss::engine::error::success);
  drain();
  check(loader.visible() && loader.operation() != gneiss::engine::error::success);
  check(world == current && session.selection() == node && session.is_dirty());
  check(session.selected_node()->display_name == "Edited" && history.can_undo());
  loader.dismiss();
  check(!loader.visible());
  check(loader.request(*app, editing, uri) == gneiss::engine::error::success);
  tick();
  check(loader.cancel(*app) == gneiss::engine::error::success);
  drain();
  check(world == current && session.selection() == node && history.can_undo());
  check(loader.progress().phase == runtime::scene_load_phase::cancelled);
  check(loader.request(*app, editing, uri) == gneiss::engine::error::success);
  drain();
  check(world != current && !session.selection().is_valid() && !history.can_undo());
  check(camera.is_valid() && session.nodes().size() == 2U);
  tick();
  std::uint64_t count{};
  // current 是旧域的借用指针，域销毁后不能再解引用。
  check(gneiss::test_support::assign_value(world->entity_count(), count).ok());
  check(loader.request(*app, editing, "asset://scenes/prefab.scene.json") ==
        gneiss::engine::error::success);
  drain();
  check(loader.operation() == gneiss::engine::error::success && session.nodes().size() == 1U &&
        session.prefab_nodes().size() == 2U && camera.is_valid());
  const auto visual_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (app->visual_assets_loading()) {
    check(std::chrono::steady_clock::now() < visual_deadline);
    tick();
  }
  editor::scene_document_snapshot before;
  check(editor::capture_scene_document(*app, session, before).ok());
  auto changed_description = before.description.description;
  const auto original_name = changed_description.objects.front().name;
  changed_description.objects.front().name = "Snapshot edit";
  std::string replacement;
  engine::meta::registry_ref registry;
  check(engine::builtin_resource_registry(registry).ok());
  const auto encode = [&] {
    std::vector<std::byte> bytes;
    engine::meta::serialization_diagnostic diagnostic;
    const auto status = engine::meta::serialize(registry, changed_description, bytes, diagnostic);
    replacement.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return status;
  };
  check(encode().ok());
  const editor::author_document_change change{
      .path = "scenes/prefab.scene.json", .baseline = {}, .replacement = replacement};
  editor::scene_document_snapshot after;
  check(editor::prepare_scene_document(before, std::span(&change, 1), after).ok());
  check(editor::restore_scene_document(*app, session, after).ok());
  check(app->native_world() == world && session.nodes().front().display_name == "Snapshot edit" &&
        session.prefab_nodes().size() == 2U);
  check(editor::restore_scene_document(*app, session, before).ok());
  check(session.nodes().front().display_name == original_name &&
        session.prefab_nodes().size() == 2U);
  // 引入未准备依赖时失败，保留输出与当前实例，不触发加载。
  changed_description.objects.front().mesh_renderer = engine::mesh_renderer_description{
      .mesh_uri = "asset://missing.mesh.json", .material_uri = "asset://missing.material.json"};
  check(encode().ok());
  const editor::author_document_change missing{
      .path = "scenes/prefab.scene.json", .baseline = {}, .replacement = replacement};
  const auto stable_scene = session.scene_handle();
  check(editor::prepare_scene_document(before, std::span(&missing, 1), after) ==
        engine::error::not_ready);
  check(session.scene_handle() == stable_scene &&
        after.description.description.objects.front().name == "Snapshot edit");
  // 无效场景的原生查询拒绝，不影响活动场景。
  check(gneiss::engine::error{app->scenes()->get_node_count(gneiss::scene_instance_id{}, &count)} ==
        gneiss::engine::error::invalid_handle);
  camera.shutdown();
  session.close();
  // 在途任务随 Application 关闭；执行器必须最后销毁。
  check(loader.request(*app, editing, uri) == gneiss::engine::error::success);
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
