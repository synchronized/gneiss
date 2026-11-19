// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/scene_query.hpp"
#include "../support/value_result.hpp"
#include "documents/editor_scene_loader.hpp"
#include "documents/scene_document_edit.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/core/tasks/scheduler.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <gneiss/engine/resource/builtin_resource_registration.hpp>

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

// 两个文档共享基础服务，World 和加载请求始终归各自宿主所有。
void multiple_worlds(gneiss::tasks::execution_mode mode) {
  tasks::scheduler scheduler({.workers = 1U, .mode = mode});
  auto main_world = create_native();
  auto preview_world = create_native();
  check(main_world && preview_world);
  editor::document_editing main_editing, preview_editing;
  editor::editor_scene_loader main_loader, preview_loader;
  editor::editor_camera main_camera, preview_camera;
  check(main_camera.initialize().ok() && preview_camera.initialize().ok());
  runtime::application_configuration config;
  config.asset_root = GNEISS_EDITOR_TEST_ASSET_ROOT;
  config.executor = &scheduler;
  // Application 先析构，全部借用的会话和执行器仍存活。
  auto created = runtime::application::create(config);
  check(created.ok());
  auto application = std::move(*created);
  auto& main = main_editing.document();
  auto& preview = preview_editing.document();
  check(main_editing.create_empty(*application, main_world->get()).ok());
  check(preview_editing.create_empty(*application, preview_world->get()).ok());
  application->add_system(app::stage::completions, "test.tasks", [&] {
    if (mode == tasks::execution_mode::cooperative)
      (void)scheduler.run_ready();
  });
  application->add_system(app::stage::update, "test.documents", [&]() -> error {
    bool changed{};
    auto status =
        main_loader.advance(*application, main_editing, **main_world, main_camera, changed);
    if (status.failed())
      return status;
    return preview_loader.advance(*application, preview_editing, **preview_world, preview_camera,
                                  changed);
  });
  application->on_stop("test.close_documents", [&] {
    (void)main_loader.cancel(*application);
    (void)preview_loader.cancel(*application);
    check(main_editing.close().ok() && preview_editing.close().ok());
    main_camera.shutdown();
    preview_camera.shutdown();
  });
  constexpr auto uri = "asset://scenes/triangle.scene.json";
  check(main_loader.request(*application, main_editing, uri).ok());
  check(preview_loader.request(*application, preview_editing, uri).ok());
  const auto drain = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
    while (main_loader.active() || preview_loader.active()) {
      check(std::chrono::steady_clock::now() < deadline);
      check(application->step().ok());
      std::this_thread::yield();
    }
    check(main_loader.operation().ok() && preview_loader.operation().ok());
  };
  drain();
  check(main.scene_handle().owner() == main_world->get());
  check(preview.scene_handle().owner() == preview_world->get());
  check(test_support::scene_query(main).nodes.size() == 2);
  check(test_support::scene_query(preview).nodes.size() == 2);
  const auto old_main = main.scene_handle();
  const auto retained_preview = preview.scene_handle();
  const auto preview_node = test_support::scene_query(preview).nodes.front().node;
  check(preview.rename_node(preview_node, "Independent preview").ok());
  check(main_editing.close().ok());
  check(!old_main.is_valid() && retained_preview.is_valid());
  check(main_loader.request(*application, main_editing, uri).ok());
  drain();
  check(main.is_open() && main.scene_handle() != old_main);
  check(preview.scene_handle() == retained_preview && preview.is_dirty());
  check(test_support::scene_query(preview).nodes.front().display_name == "Independent preview");
  const auto reopened = main.scene_handle();
  application->request_exit();
  check(application->run().ok());
  check(!main.is_open() && !preview.is_open());
  check(!reopened.is_valid() && !retained_preview.is_valid());
}

void run(gneiss::tasks::execution_mode mode) {
  gneiss::tasks::scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<runtime::application> app;
  runtime::application_configuration desc;
  const std::string root = GNEISS_EDITOR_TEST_ASSET_ROOT;
  desc.asset_root = root;
  desc.executor = &scheduler;
  check(gneiss::test_support::assign_value(runtime::application::create(desc), app) ==
        gneiss::error::success);

  auto created_world = gneiss::create_native();
  check(bool(created_world));
  auto owner = std::move(*created_world);
  editor::document_editing editing;
  auto& session = editing.document();
  editor::editor_camera camera;
  auto& history = editing.history();
  editor::editor_scene_loader loader;
  auto world = owner.get();
  check(session.create_empty(*app, world) == gneiss::error::success);
  check(camera.initialize() == gneiss::error::success);
  check(loader.state() == editor::scene_open_state::idle);
  const auto original = world;
  const auto initial_scene = session.scene_handle();
  constexpr std::string_view uri = "asset://scenes/triangle.scene.json";
  auto tick = [&] {
    if (mode == gneiss::tasks::execution_mode::cooperative)
      (void)scheduler.run_ready();
    check(app->step().ok());
    bool changed{};
    check(loader.advance(*app, editing, *world, camera, changed) == gneiss::error::success);
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
  check(!loader.visible() && loader.operation() == gneiss::error::success && world == original);
  check(loader.state() == editor::scene_open_state::opened);
  check(session.is_open() && !session.is_dirty() && session.uri() == uri);
  check(session.scene_handle().owner() == world);
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
  check(history.undo() == gneiss::error::success && history.redo() == gneiss::error::success);
  check(loader.request(*app, editing, "asset://missing.scene.json") == gneiss::error::success);
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
  check(session.scene_handle().owner() == world &&
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
  // 外部 ID 借用失败不影响活动场景。
  auto invalid_scene = gneiss::scene_instance::borrow(*world, {});
  check(!invalid_scene && invalid_scene.error() == gneiss::error::invalid_handle);
  check(session.scene_handle().is_valid());
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
  multiple_worlds(gneiss::tasks::execution_mode::thread_pool);
  multiple_worlds(gneiss::tasks::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
