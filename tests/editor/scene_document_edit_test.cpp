// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/async_editor_save.hpp"
#include "commands/prefab_authoring.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/resource/asset_file.hpp"
#include "engine/resource/scene/prefab_description.hpp"
#include <cstdio>
#include <fstream>
#include <source_location>
namespace e = gneiss;
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
enum class scenario { success, failure, modified, world_modified, closed, switched, cancelled };
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
  e::game_object_description root;
  root.uuid = source.root_uuid;
  root.name = "Root";
  root.components.set(e::mesh_component_description{
      "asset://mesh.json",
      mode == scenario::failure ? "asset://missing.json" : "asset://material.json"});
  source.objects.push_back(root);
  write(files.path / "source.prefab.json",
        text(*gneiss::asset_file::serialize_bytes(source)));
  e::scene_description scene;
  scene.uuid = id(3);
  e::game_object_description instance;
  instance.uuid = id(4);
  instance.name = "Instance";
  instance.prefab.emplace();
  instance.prefab->uri = "asset://source.prefab.json";
  instance.prefab->component_overrides.push_back(
      {root.uuid,
       e::mesh_component_description::type_guid,
       {},
       root.components.identity(e::mesh_component_description::type_guid)});
  scene.objects.push_back(instance);
  const auto json = text(*gneiss::asset_file::serialize_bytes(scene));
  write(files.path / "scene.json", json);
  author_scene editing;
  check(editing.open(files.path, "asset://scene.json"));
  auto changes = ed::prepare_restore_prefab_component(json, "scene.json", instance.uuid, root.uuid,
                                                      e::mesh_component_description::type_guid);
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
  if (mode == scenario::world_modified) {
    // 运行时变化不改变文档基线，也不能被下一次编辑带入保存内容。
    const auto node = editing.document.find_prefab_root(instance.uuid);
    check(bool(node));
    auto changed = node->local_transform;
    changed.translation.x = 17;
    check(editing.application->native_world()
              ->find(gneiss::entity_id{(node->node).get()})
              .set_local_transform(changed)
              .ok());
  }
  if (mode == scenario::closed || mode == scenario::switched)
    editing.document.close();
  if (mode == scenario::switched) {
    // 关闭文档后推进操作释放自身槽位，再启动新场景加载。
    check(operation.advance(*editing.application, editing.document, history) ==
          e::error::invalid_state);
    check(open_document(editing.document, *editing.application, editing.application->native_world(),
                        "asset://scene.json")
              .ok());
  }
  if (mode == scenario::cancelled)
    operation.cancel();
  // 首次推进立即采用合法描述，不驱动文件或资产完成队列。
  e::error status = operation.advance(*editing.application, editing.document, history);
  if (mode == scenario::success || mode == scenario::failure || mode == scenario::world_modified)
    check(status.ok());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (status == e::error::not_ready) {
    check(std::chrono::steady_clock::now() < deadline);
    (void)editing.application->run(1);
    status = operation.advance(*editing.application, editing.document, history);
    std::this_thread::yield();
  }
  check(operation.advance(*editing.application, editing.document, history) == status);
  if (mode == scenario::success || mode == scenario::failure || mode == scenario::world_modified) {
    check(status.ok() && history.size() == 1 && redo_count == 1);
    auto node = editing.document.find_prefab_root(instance.uuid);
    check(node && editing.document.components(node->node)->get<e::mesh_component_description>());
    auto snapshot = ed::capture_scene_document(*editing.application, editing.document);
    check(snapshot && snapshot->assets.size() == 2);
    std::string saved;
    check(save_document(editing.document, files.path, &saved).ok());
    check(!saved.empty());
    if (mode == scenario::failure) {
      auto missing = editing.application->assets().load(
          e::render_resource_source{"asset://missing.json", e::render_resource_kind::material});
      check(bool(missing));
      while (missing->status() == e::error::not_ready) {
        check(std::chrono::steady_clock::now() < deadline);
        (void)editing.application->run(1);
        std::this_thread::yield();
      }
      check(missing->status().failed());
      check(bool(ed::capture_scene_document(*editing.application, editing.document)));
      check(save_document(editing.document, files.path).ok());
    }
    // 文件变坏或资源加载失败也不能影响历史重放：快照保活资源身份。
    write(files.path / "mesh.json", "broken");
    write(files.path / "material.json", "broken");
    check(history.undo().ok() && undo_count == 0);
    node = editing.document.find_prefab_root(instance.uuid);
    check(node && !editing.document.components(node->node)->get<e::mesh_component_description>());
    check(history.redo().ok() && redo_count == 1);
    node = editing.document.find_prefab_root(instance.uuid);
    check(node && editing.document.components(node->node)->get<e::mesh_component_description>());
  } else {
    check(status.failed() && history.size() == 0 && redo_count == 0 && undo_count == 0);
    if (mode == scenario::cancelled)
      check(status == e::error::cancelled);
    if (mode == scenario::modified || mode == scenario::closed || mode == scenario::switched)
      check(status == e::error::invalid_state);
    if (editing.document.is_open()) {
      const auto node = editing.document.find_prefab_root(instance.uuid);
      check(node && !editing.document.components(node->node)->get<e::mesh_component_description>());
      if (mode == scenario::modified)
        check(node->display_name == "Changed");
      if (mode == scenario::world_modified)
        check(node->local_transform.translation.x == 17);
      if (mode != scenario::switched)
        check(editing.document.scene_handle() == scene_id);
    }
  }
  check(history.clear().ok());
  // 文档恢复不占用加载组槽位；析构未采用的候选不影响后续请求。
  if (mode == scenario::cancelled) {
    {
      ed::scene_document_edit abandoned;
      check(abandoned.begin(*editing.application, editing.document, *changes, "放弃恢复").ok());
    }
    ed::scene_document_edit retry;
    check(retry.begin(*editing.application, editing.document, *changes, "重试恢复").ok());
    retry.cancel();
  }
  const e::render_resource_source source_asset{"asset://material.json",
                                               e::render_resource_kind::material};
  auto retry = editing.application->load_assets({&source_asset, 1}, true);
  check(retry && bool(*retry));
  retry->reset();
  editing.document.close();
}
int main() try {
  for (auto mode :
       {scenario::success, scenario::failure, scenario::modified, scenario::world_modified,
        scenario::closed, scenario::switched, scenario::cancelled})
    run(mode);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
