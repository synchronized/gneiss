// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/scene_query.hpp"
#include "../support/value_result.hpp"
#include "documents/editor_scene_loader.hpp"
#include "documents/scene_document_edit.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <gneiss/engine/resource/builtin_resource_registration.hpp>

#include "../support/scene_access.hpp"
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/scene/world.hpp>

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

void run(gneiss::tasks::execution_mode mode) {
  gneiss::tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<runtime::application> app;
  runtime::application_configuration desc;
  const std::string root = GNEISS_EDITOR_TEST_ASSET_ROOT;
  desc.asset_root = root;
  check(gneiss::test_support::assign_value(runtime::application::create(desc), app) ==
        gneiss::error::success);
  check(app->attach_task_executor(scheduler) == gneiss::error::success);
  editor::document_editing editing;
  auto& session = editing.document();
  editor::editor_camera camera;
  auto& history = editing.history();
  editor::editor_scene_loader loader;
  auto world = app->native_world();
  check(session.create_empty(*app, world) == gneiss::error::success);
  check(camera.initialize() == gneiss::error::success);
  check(loader.state() == editor::scene_open_state::idle);
  const auto original = world;
  const auto initial_scene = session.scene_handle();
  constexpr std::string_view uri = "asset://scenes/triangle.scene.json";
  auto tick = [&] {
    if (mode == gneiss::tasks::execution_mode::cooperative)
      (void)scheduler.run_ready();
    check(gneiss::error{app->run(1U)} == gneiss::error::success);
    bool changed{};
    check(loader.advance(*app, editing, world, camera, changed) == gneiss::error::success);
    std::this_thread::yield();
  };
  auto drain = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
    while (loader.active()) {
      check(std::chrono::steady_clock::now() < deadline);
      tick();
    }
  };
  // 尚未提交资产请求时也可以取消，不改变当前文档。
  check(loader.request(*app, editing, uri).ok());
  check(loader.cancel(*app).ok());
  check(!loader.active() && loader.state() == editor::scene_open_state::cancelled);
  check(session.scene_handle() == initial_scene);
  loader.dismiss();
  check(!loader.visible());
  check(loader.request(*app, editing, uri) == gneiss::error::success);
  check(loader.active() && loader.visible() && world == original);
  check(loader.state() == editor::scene_open_state::loading);
  check(loader.request(*app, editing, uri) == gneiss::error::not_ready);
  drain();
  check(!loader.visible() && loader.operation() == gneiss::error::success &&
        world == original);
  check(loader.state() == editor::scene_open_state::opened);
  check(session.is_open() && !session.is_dirty() && session.uri() == uri);
  check(!gneiss::scene_instance::borrow(*world, (initial_scene).id()));
  check(gneiss::test_support::scene_query(session).nodes.size() == 2U && camera.is_valid());
  const auto current = world;
  const auto node = gneiss::test_support::scene_query(session).nodes[1].node;
  check(session.select(node) == gneiss::error::success);
  check(session.rename_node(node, "Edited") == gneiss::error::success);
  auto command =
      std::make_unique<editor::functional_editor_command>(editor::functional_editor_command::desc{
          .label = "Rename",
          .undo = [&] { return session.rename_node(node, "Original"); },
          .redo = [&] { return session.rename_node(node, "Edited"); },
          .merge_key = {}});
  check(history.record(std::move(command)) == gneiss::error::success);
  check(history.undo() == gneiss::error::success &&
        history.redo() == gneiss::error::success);
  check(loader.request(*app, editing, "asset://missing.scene.json") ==
        gneiss::error::success);
  drain();
  check(loader.visible() && loader.operation() != gneiss::error::success);
  check(loader.state() == editor::scene_open_state::failed);
  check(world == current && session.selection() == node && session.is_dirty());
  check(session.selected_node()->display_name == "Edited" && history.can_undo());
  loader.dismiss();
  check(!loader.visible());
  check(loader.request(*app, editing, uri) == gneiss::error::success);
  tick();
  check(loader.cancel(*app) == gneiss::error::success);
  drain();
  check(world == current && session.selection() == node && history.can_undo());
  check(loader.state() == editor::scene_open_state::cancelled);
  check(loader.request(*app, editing, uri) == gneiss::error::success);
  drain();
  check(world == current && !session.selection().is_valid() && !history.can_undo());
  check(camera.is_valid() && gneiss::test_support::scene_query(session).nodes.size() == 2U);
  tick();
  std::uint64_t count{};
  // World 保持不变，旧场景对象由文档关闭时卸载。
  check(!world->object(node));

  check(loader.request(*app, editing, "asset://scenes/prefab.scene.json") ==
        gneiss::error::success);
  drain();
  check(loader.operation() == gneiss::error::success &&
        gneiss::test_support::scene_query(session).nodes.size() == 1U &&
        gneiss::test_support::scene_query(session).prefab_nodes.size() == 2U && camera.is_valid());
  const auto visual_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (loader.visual_loading()) {
    check(std::chrono::steady_clock::now() < visual_deadline);
    tick();
  }
  auto captured = editor::capture_scene_document(*app, session);
  check(bool(captured));
  auto before = std::move(*captured);
  auto restored_description = before.description();
  check(restored_description.has_value());
  auto changed_description = std::move(*restored_description);
  const auto original_name = changed_description.objects.front().name;
  changed_description.objects.front().name = "Snapshot edit";
  std::string replacement;
  const auto encode = [&] {
    std::vector<std::byte> bytes;
    gneiss::serialization::diagnostic diagnostic;
    const auto status = gneiss::test_support::assign_decoded(
        gneiss::asset_file::serialize_bytes(changed_description), bytes, diagnostic);
    replacement.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return status;
  };
  check(encode().ok());
  const editor::author_document_change change{
      .path = "scenes/prefab.scene.json", .baseline = {}, .replacement = replacement};
  auto prepared = editor::prepare_scene_document(before, std::span(&change, 1));
  check(prepared && prepared->missing.empty());
  auto after = std::move(prepared->snapshot);
  check(editor::restore_scene_document(*app, session, after).ok());
  check(app->native_world() == world &&
        gneiss::test_support::scene_query(session).nodes.front().display_name == "Snapshot edit" &&
        gneiss::test_support::scene_query(session).prefab_nodes.size() == 2U);
  check(editor::restore_scene_document(*app, session, before).ok());
  check(gneiss::test_support::scene_query(session).nodes.front().display_name == original_name &&
        gneiss::test_support::scene_query(session).prefab_nodes.size() == 2U);
  // 引入未准备依赖时返回清单，保留原快照与当前实例，不触发加载。
  changed_description.objects.front().components.set(gneiss::mesh_component_description{
      "asset://missing.mesh.json", "asset://missing.material.json"});
  check(encode().ok());
  const editor::author_document_change missing{
      .path = "scenes/prefab.scene.json", .baseline = {}, .replacement = replacement};
  const auto stable_scene = session.scene_handle();
  auto unresolved = editor::prepare_scene_document(before, std::span(&missing, 1));
  check(unresolved && unresolved->missing.size() == 2);
  check(session.scene_handle() == stable_scene &&
        after.description()->objects.front().name == "Snapshot edit");
  // 无效场景的原生查询拒绝，不影响活动场景。
  check(gneiss::error{gneiss::test_support::take_value(
            gneiss::test_support::scene_access{*app}->get_node_count(gneiss::scene_instance{}),
            count)} == gneiss::error::invalid_handle);
  camera.shutdown();
  session.close();
  // 在途任务随 Application 关闭；执行器必须最后销毁。
  check(loader.request(*app, editing, uri) == gneiss::error::success);
  tick();
}
} // namespace

int main() try {
  if (gneiss::initialize_meta().failed())
    return 99;
  run(gneiss::tasks::execution_mode::thread_pool);
  run(gneiss::tasks::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
