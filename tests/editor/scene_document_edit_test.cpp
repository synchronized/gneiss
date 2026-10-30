// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/async_editor_save.hpp"
#include "commands/prefab_authoring.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/resource/scene/prefab_codec.hpp"
#include <cstdio>
#include <fstream>
#include <source_location>
namespace e = gneiss::engine;
namespace ed = gneiss::editor;
using namespace gneiss::test_support;
void check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok)
    throw std::runtime_error("异步恢复失败，行=" + std::to_string(at.line()));
}
void write(const std::filesystem::path& path, std::string_view text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
  check(bool(out));
}
std::string text(const std::vector<std::byte>& bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
enum class scenario { success, failure, modified, closed, switched, cancelled };
void run(scenario mode) {
  struct directory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("gneiss-restore-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    directory() { std::filesystem::create_directories(path); }
    ~directory() {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
  } files;
  write(
      files.path / "mesh.json",
      R"({"format":"gneiss.mesh","version":3,"topology":"triangle_list","vertices":[[0,0,0],[1,0,0],[0,1,0]],"uvs":[[0,0],[0,0],[0,0]],"normals":[[0,0,1],[0,0,1],[0,0,1]]})");
  write(
      files.path / "material.json",
      R"({"format":"gneiss.material","version":6,"parameters":{"metallic":0,"roughness":1,"color":[1,0,0,1]},"texture_uris":["","","","",""]})");
  const auto id = [](std::uint8_t value) { return e::guid::from_bytes({90, value}); };
  // 初始化元数据后生成与真实加载流程相同的来源文件。
  check(e::initialize_meta().ok());
  e::prefab_description source;
  source.uuid = id(1);
  source.root_uuid = id(2);
  e::object_description root;
  root.uuid = source.root_uuid;
  root.name = "Root";
  root.components.set(e::mesh_renderer_description{
      "asset://mesh.json",
      mode == scenario::failure ? "asset://missing.json" : "asset://material.json"});
  source.objects.push_back(root);
  write(files.path / "source.prefab.json", text(*e::encode_prefab(source)));
  e::scene_description scene;
  scene.uuid = id(3);
  e::object_description instance;
  instance.uuid = id(4);
  instance.name = "Instance";
  instance.prefab.emplace();
  instance.prefab->uri = "asset://source.prefab.json";
  instance.prefab->component_overrides.push_back(
      {root.uuid, e::mesh_renderer_description::type_guid, {}});
  scene.objects.push_back(instance);
  const auto json = text(*e::encode_scene(scene));
  write(files.path / "scene.json", json);
  author_scene editing;
  check(editing.open(files.path, "asset://scene.json"));
  auto changes = ed::prepare_restore_prefab_component(json, "scene.json", instance.uuid, root.uuid,
                                                      e::mesh_renderer_description::type_guid);
  check(bool(changes));
  ed::editor_command_history history;
  ed::scene_document_edit operation;
  int redo_count = 0, undo_count = 0;
  check(operation
            .begin(*editing.application, editing.document, *changes, "恢复网格",
                   [&](bool undo) {
                     if (undo)
                       ++undo_count;
                     else
                       ++redo_count;
                   })
            .ok());
  check(history.size() == 0);
  const auto scene_id = editing.document.scene_handle();
  if (mode == scenario::modified)
    check(editing.document
              .rename_node(editing.document.find_prefab_root(instance.uuid)->node, "Changed")
              .ok());
  if (mode == scenario::closed || mode == scenario::switched)
    editing.document.close();
  if (mode == scenario::switched) {
    gneiss::scene_instance_id replacement;
    check(load_scene(*editing.application, "asset://scene.json", &replacement).ok());
    check(editing.document
              .adopt(*editing.application, editing.application->native_world(), replacement,
                     "asset://scene.json")
              .ok());
  }
  if (mode == scenario::cancelled)
    operation.cancel();
  e::error status = e::error::not_ready;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (status == e::error::not_ready) {
    check(std::chrono::steady_clock::now() < deadline);
    editing.application->advance_scene_load();
    status = operation.advance(*editing.application, editing.document, history);
    std::this_thread::yield();
  }
  check(operation.advance(*editing.application, editing.document, history) == status);
  if (mode == scenario::success) {
    check(status.ok() && history.size() == 1 && redo_count == 1);
    auto* node = editing.document.find_prefab_root(instance.uuid);
    check(node && editing.document.components(node->node)->get<e::mesh_renderer_description>());
    // 文件变坏也不能影响历史重放：快照持有已加载资源。
    write(files.path / "mesh.json", "broken");
    write(files.path / "material.json", "broken");
    check(history.undo().ok() && undo_count == 1);
    node = editing.document.find_prefab_root(instance.uuid);
    check(node && !editing.document.components(node->node)->get<e::mesh_renderer_description>());
    check(history.redo().ok() && redo_count == 2);
    node = editing.document.find_prefab_root(instance.uuid);
    check(node && editing.document.components(node->node)->get<e::mesh_renderer_description>());
  } else {
    check(status.failed() && history.size() == 0 && redo_count == 0 && undo_count == 0);
    if (mode == scenario::cancelled)
      check(status == e::error::cancelled);
    if (mode == scenario::modified || mode == scenario::closed || mode == scenario::switched)
      check(status == e::error::invalid_state);
    if (editing.document.is_open()) {
      const auto* node = editing.document.find_prefab_root(instance.uuid);
      check(node && !editing.document.components(node->node)->get<e::mesh_renderer_description>());
      if (mode == scenario::modified)
        check(node->display_name == "Changed");
      if (mode != scenario::switched)
        check(editing.document.scene_handle() == scene_id);
    }
  }
  check(history.clear().ok());
  editing.document.close();
}
int main() try {
  for (auto mode : {scenario::success, scenario::failure, scenario::modified, scenario::closed,
                    scenario::switched, scenario::cancelled})
    run(mode);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
