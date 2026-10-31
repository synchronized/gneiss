// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../support/async_document.hpp"
#include "commands/editor_asset_edit.hpp"
#include "engine/core/error.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
#include "runtime/loading/prefab_placement.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss;
void check(bool value, std::source_location location = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("资产编辑验证失败：" + std::to_string(location.line()));
  }
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-edit-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture() {
    std::filesystem::create_directories(root);
    std::ofstream(root / "mesh.json")
        << R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[0,0],[0,0]],"normals":[[0.0,0.0,1.0],[0.0,0.0,1.0],[0.0,0.0,1.0]]})";
    std::ofstream(root / "a.json")
        << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,0,0,1]},"texture_uris":["","","","",""]})";
    std::ofstream(root / "b.json")
        << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,1,0,1]},"texture_uris":["","","","",""]})";
    std::ofstream(root / "scene.json")
        << R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://mesh.json","material":"asset://a.json"}}]}]})";
  }
  ~fixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};
void run(gneiss::engine::execution_mode mode) {
  fixture files;
  gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::runtime::application_configuration desc;
  const auto root = files.root.string();
  desc.asset_root = root;
  check(gneiss::engine::error{gneiss::runtime::application::create(desc, app)} ==
        gneiss::engine::error::success);
  check(app->attach_task_executor(scheduler) == gneiss::engine::error::success);
  gneiss::engine::world* world{};
  check((world = app->native_world()) != nullptr);
  editor::scene_document session;
  editor::editor_command_history history;
  editor::editor_asset_edit edit;
  check(gneiss::test_support::open_document(session, *app, world, "asset://scene.json",
                                            &scheduler) == gneiss::engine::error::success);
  const auto uuid = session.nodes().front().uuid;
  const auto drive = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (edit.active()) {
      if (mode == gneiss::engine::execution_mode::cooperative) {
        (void)scheduler.run_ready();
      }
      edit.advance(*app, session, history);
      check(std::chrono::steady_clock::now() < deadline);
      std::this_thread::yield();
    }
  };
  const auto request = [&](std::string_view material) {
    return edit.request_property(
        *app, session, history,
        {.object_uuid = uuid, .component_type = engine::mesh_renderer_description::type_guid},
        {.path = {engine::meta::property_id{2}}, .data = engine::value{std::string(material)}});
  };
  const auto binding = [&] {
    gneiss::engine::scene_instances::mesh_binding value;
    check(app->capture_mesh_binding(session.scene_handle(), session.find_node(uuid)->node, value) ==
          gneiss::engine::error::success);
    return value;
  };
  const auto initial_binding = binding();
  check(request("asset://b.json") == gneiss::engine::error::success && edit.active() &&
        edit.visible());
  check(history.size() == 0U &&
        session.nodes()
                .front()
                .components.get<gneiss::engine::mesh_renderer_description>()
                ->material_uri == "asset://a.json");
  check(request("asset://b.json") == gneiss::engine::error::not_ready);
  drive();
  check(edit.operation() == gneiss::engine::error::success && !edit.visible() &&
        history.size() == 1U);
  check(session.nodes()
            .front()
            .components.get<gneiss::engine::mesh_renderer_description>()
            ->material_uri == "asset://b.json");
  const auto changed_binding = binding();
  check(initial_binding.material != changed_binding.material);
  std::ofstream(files.root / "a.json") << "broken";
  std::ofstream(files.root / "b.json") << "broken";
  check(history.undo() == gneiss::engine::error::success &&
        session.nodes()
                .front()
                .components.get<gneiss::engine::mesh_renderer_description>()
                ->material_uri == "asset://a.json");
  check(binding().material == initial_binding.material);
  check(history.redo() == gneiss::engine::error::success &&
        session.nodes()
                .front()
                .components.get<gneiss::engine::mesh_renderer_description>()
                ->material_uri == "asset://b.json");
  check(binding().material == changed_binding.material);
  // 新材质准备期间撤销前一次编辑；迟到结果不得覆盖撤销后的确切版本。
  std::ofstream(files.root / "c.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,0,1,1]},"texture_uris":["","","","",""]})";
  check(request("asset://c.json") == gneiss::engine::error::success && edit.active());
  check(history.undo() == gneiss::engine::error::success);
  drive();
  check(edit.operation() == gneiss::engine::error::invalid_state && history.size() == 1U &&
        binding().material == initial_binding.material);
  check(history.redo() == gneiss::engine::error::success &&
        binding().material == changed_binding.material);
  edit.dismiss();
  check(history.undo() == gneiss::engine::error::success);
  check(request("asset://b.json") == gneiss::engine::error::success && !edit.active() &&
        !edit.visible());
  const auto count = history.size();
  check(request("asset://missing.json") == gneiss::engine::error::success);
  drive();
  check(edit.operation().failed() && history.size() == count &&
        session.nodes()
                .front()
                .components.get<gneiss::engine::mesh_renderer_description>()
                ->material_uri == "asset://b.json");
  edit.dismiss();
  check(request("asset://missing.json") == gneiss::engine::error::success);
  edit.cancel(*app);
  drive();
  check(!edit.visible() && history.size() == count);
  // 创建同样先准备，成功才产生节点；命令持有资源引用，坏文件不影响当前成功内容的复用。
  const editor::mesh_asset_reference new_assets{.mesh_uri = "asset://mesh.json",
                                                .material_uri = "asset://b.json"};
  check(edit.request_add(*app, session, history, "new node", new_assets) ==
        gneiss::engine::error::success);
  check(!edit.active() && session.nodes().size() == 2U && history.size() == count + 1U);
  drive();
  check(edit.operation() == gneiss::engine::error::success && session.nodes().size() == 2U);
  const auto created_uuid = session.nodes().back().uuid;
  std::ofstream(files.root / "mesh.json") << "broken";
  check(history.undo() == gneiss::engine::error::success &&
        session.find_node(created_uuid) == nullptr);
  check(history.redo() == gneiss::engine::error::success &&
        session.find_node(created_uuid) != nullptr);
  editor::editor_command_history disabled{0U};
  check(edit.request_add(*app, session, disabled, "rejected", new_assets) ==
        gneiss::engine::error::invalid_argument);
  check(!edit.active() && session.nodes().size() == 2U);
  check(edit.request_add(*app, session, history, "ready node", new_assets) ==
        gneiss::engine::error::success);
  check(!edit.active() && !edit.visible() && session.nodes().size() == 3U);
  const auto creation_count = history.size();
  check(
      edit.request_add(*app, session, history, "missing",
                       {.mesh_uri = "asset://mesh.json", .material_uri = "asset://missing.json"}) ==
      gneiss::engine::error::success);
  drive();
  check(edit.operation().failed() && session.nodes().size() == 3U &&
        history.size() == creation_count);
  edit.dismiss();
  // 文档已经关闭的迟到成功回执不能提交历史或采用新版本。
  std::ofstream(files.root / "c.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[0,0,1,1]},"texture_uris":["","","","",""]})";
  check(request("asset://c.json") == gneiss::engine::error::success);
  session.close();
  drive();
  check(edit.operation() == gneiss::engine::error::invalid_state &&
        history.size() == creation_count);
  gneiss::engine::render_asset_ref unused;
  check(app->query_ready_asset(
            {.uri = "asset://c.json", .type = gneiss::engine::asset_type::material}, unused) ==
            gneiss::engine::error::not_ready &&
        !unused);
  history.clear();
  // 旧请求还在准备时切换到另一个有效场景；不能污染新场景或采用旧候选。
  check(session.create_empty(*app, world) == gneiss::engine::error::success);
  std::ofstream(files.root / "d.json")
      << R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,1,0,1]},"texture_uris":["","","","",""]})";
  check(edit.request_add(*app, session, history, "stale",
                         {.mesh_uri = "asset://mesh.json", .material_uri = "asset://d.json"}) ==
        gneiss::engine::error::success);
  check(edit.active());
  const auto previous_scene = session.scene_handle();
  check(session.create_empty(*app, world) == gneiss::engine::error::success);
  const auto new_scene = session.scene_handle();
  check(new_scene != previous_scene);
  drive();
  check(edit.operation() == gneiss::engine::error::invalid_state &&
        session.scene_handle() == new_scene && session.nodes().empty() && history.size() == 0U);
  check(app->query_ready_asset(
            {.uri = "asset://d.json", .type = gneiss::engine::asset_type::material}, unused) ==
        gneiss::engine::error::not_ready);
  session.close();
}
void prefab_edits(engine::execution_mode mode) {
  namespace e = engine;
  fixture files;
  e::task_scheduler scheduler({.workers = 1U, .mode = mode});
  std::shared_ptr<runtime::application> app;
  runtime::application_configuration config;
  const auto root_path = files.root.string();
  config.asset_root = root_path;
  check(runtime::application::create(config, app).ok());
  check(app->attach_task_executor(scheduler).ok());
  const auto uuid = [](unsigned n) {
    e::guid::byte_array bytes{};
    bytes[15] = static_cast<std::uint8_t>(n);
    return e::guid{bytes};
  };
  const auto write = [&](const char* name, const auto& value) {
    auto encoded = e::meta::serialize(value);
    check(bool(encoded));
    std::ofstream file(files.root / name, std::ios::binary);
    file.write(reinterpret_cast<const char*>(encoded->data()),
               static_cast<std::streamsize>(encoded->size()));
  };
  e::prefab_description prefab;
  prefab.uuid = uuid(1);
  prefab.root_uuid = uuid(2);
  e::object_description source;
  source.uuid = prefab.root_uuid;
  source.name = "root";
  source.components.set(e::mesh_renderer_description{"asset://mesh.json", "asset://a.json"});
  prefab.objects.push_back(source);
  source.uuid = uuid(3);
  source.parent_uuid = prefab.root_uuid;
  source.name = "child";
  prefab.objects.push_back(source);
  write("prefab.json", prefab);
  e::scene_description scene;
  scene.uuid = uuid(4);
  e::object_description author;
  author.uuid = uuid(5);
  author.name = "first";
  author.prefab.emplace();
  author.prefab->uri = "asset://prefab.json";
  scene.objects.push_back(author);
  author.uuid = uuid(6);
  author.name = "second";
  scene.objects.push_back(author);
  write("prefab-scene.json", scene);
  editor::scene_document document;
  editor::editor_command_history history;
  editor::editor_asset_edit edit;
  check(test_support::open_document(document, *app, app->native_world(),
                                    "asset://prefab-scene.json", &scheduler)
            .ok());
  const editor::component_edit_target child{.instance_uuid = uuid(5),
                                            .source_uuid = uuid(3),
                                            .component_type =
                                                e::mesh_renderer_description::type_guid};
  auto root = child;
  root.source_uuid = prefab.root_uuid;
  const auto binding = [&](const editor::component_edit_target& target) {
    e::scene_mesh_binding result;
    const auto* node = document.find_prefab_source(target.instance_uuid, target.source_uuid);
    check(node != nullptr);
    check(app->capture_mesh_binding(document.scene_handle(), node->node, result).ok());
    return result;
  };
  const auto drive = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (edit.active()) {
      if (mode == e::execution_mode::cooperative)
        (void)scheduler.run_ready();
      edit.advance(*app, document, history);
      check(std::chrono::steady_clock::now() < deadline);
      std::this_thread::yield();
    }
  };
  const auto request = [&](const auto& target, std::string uri) {
    return edit.request_property(*app, document, history, target,
                                 {.path = {e::meta::property_id{2}}, .data = e::value{uri}});
  };
  const auto capture = [&] {
    e::scene_description current;
    check(app->scenes()->capture_description(document.scene_handle(), current).ok());
    return current;
  };
  const auto overrides = [&] {
    const auto current = capture();
    const auto found = std::ranges::find(current.objects, uuid(5), &e::object_description::uuid);
    check(found != current.objects.end() && found->prefab.has_value());
    return found->prefab->overrides.size();
  };
  const auto before = binding(child);
  check(request(child, "").ok() && !edit.active() && !binding(child).material);
  check(history.undo().ok() && binding(child).material == before.material);
  check(history.redo().ok() && !binding(child).material);
  check(history.undo().ok() && binding(child).material == before.material);
  history.clear();
  check(request(child, "asset://b.json").ok() && edit.active());
  check(binding(child).material == before.material && history.size() == 0);
  drive();
  check(edit.operation().ok() && history.size() == 1);
  const auto changed = binding(child);
  check(changed.material != before.material);
  check(overrides() == 1);
  auto second = child;
  second.instance_uuid = uuid(6);
  check(binding(second).material == before.material);
  check(document.select(document.find_prefab_source(second.instance_uuid, second.source_uuid)->node)
            .ok());
  check(history.undo().ok() && binding(child).material == before.material);
  check(overrides() == 0);
  check(history.redo().ok() && binding(child).material == changed.material);
  check(request(root, "asset://b.json").ok() && !edit.active());
  check(overrides() == 2);
  const auto count = history.size();
  check(request(root, "asset://b.json").ok() && history.size() == count);
  check(request(child, "asset://missing.json").ok());
  drive();
  check(edit.operation().failed() && history.size() == count &&
        binding(child).material == changed.material);
  edit.dismiss();
  std::shared_ptr<e::scene_subtree_snapshot> captured;
  check(runtime::prefab_placement::capture(*app, uuid(5), document.scene_handle(), captured).ok());
  const auto captured_author = captured->description.objects.front();
  const auto captured_source = captured->resources.at(uuid(5)).prefab;
  check(captured_source->objects[1].components.get<e::mesh_renderer_description>()->material_uri ==
        "asset://a.json");
  // 来源刷新快照必须持有覆盖资源，撤销/重做不依赖文件或弱缓存。
  std::shared_ptr<const e::scene_subtree_snapshot> frozen;
  e::prepared_prefab_description prepared;
  prepared.resource = captured_source;
  check(runtime::prefab_placement::freeze(*app, prepared, captured_author, {}, frozen).ok());
  check(frozen->resources.at(uuid(5)).source_nodes[1].material == changed.material);
  scene_node_id replaced;
  check(runtime::prefab_placement::apply(*app, document.scene_handle(), *frozen, false, true,
                                         replaced)
            .ok());
  check(document.synchronize_nodes().ok());
  check(binding(child).material == changed.material);
  check(history.undo().ok() && binding(root).material == before.material);
  check(history.redo().ok() && binding(root).material == changed.material);
  // 网格 URI 与材质 URI 走同一属性请求，加载完成前继续使用原网格。
  std::filesystem::copy_file(files.root / "mesh.json", files.root / "mesh2.json");
  check(edit.request_property(*app, document, history, child,
                              {.path = {e::meta::property_id{1}},
                               .data = e::value{std::string("asset://mesh2.json")}})
            .ok());
  check(edit.active() && binding(child).mesh == before.mesh);
  drive();
  check(edit.operation().ok() && e::asset_uri(binding(child).mesh) == "asset://mesh2.json");
  // 文件保存后通过实际异步场景加载链重建；两实例的差异必须保留。
  write("saved.json", capture());
  std::shared_ptr<runtime::application> reopened_app;
  check(runtime::application::create(config, reopened_app).ok());
  check(reopened_app->attach_task_executor(scheduler).ok());
  editor::scene_document reopened;
  check(test_support::open_document(reopened, *reopened_app, reopened_app->native_world(),
                                    "asset://saved.json", &scheduler)
            .ok());
  e::scene_mesh_binding loaded;
  const auto* loaded_child = reopened.find_prefab_source(uuid(5), uuid(3));
  check(loaded_child != nullptr);
  check(
      reopened_app->capture_mesh_binding(reopened.scene_handle(), loaded_child->node, loaded).ok());
  check(e::asset_uri(loaded.material) == "asset://b.json");
  check(e::asset_uri(loaded.mesh) == "asset://mesh2.json");
  const auto* loaded_second = reopened.find_prefab_source(uuid(6), uuid(3));
  check(loaded_second != nullptr);
  check(reopened_app->capture_mesh_binding(reopened.scene_handle(), loaded_second->node, loaded)
            .ok());
  check(e::asset_uri(loaded.material) == "asset://a.json");
  check(history.undo().ok() && binding(child).mesh == before.mesh);
  check(request(child, "asset://a.json").ok());
  check(overrides() == 1);
  check(history.undo().ok() && binding(child).material == changed.material);
  history.clear();
  reopened.close();
  document.close();
}
}
int main() try {
  run(gneiss::engine::execution_mode::cooperative);
  run(gneiss::engine::execution_mode::thread_pool);
  prefab_edits(gneiss::engine::execution_mode::cooperative);
  prefab_edits(gneiss::engine::execution_mode::thread_pool);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
