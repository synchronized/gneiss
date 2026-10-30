// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/async_editor_save.hpp"
#include "../support/movement_component.hpp"
#include "commands/editor_command_history.hpp"
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
    throw std::runtime_error("Prefab 作者操作失败，行=" + std::to_string(at.line()));
}
std::string text(const std::vector<std::byte>& data) {
  return {reinterpret_cast<const char*>(data.data()), data.size()};
}
void write(const std::filesystem::path& path, std::string_view value) {
  std::ofstream stream(path, std::ios::binary);
  stream << value;
  check(bool(stream));
}
int main() try {
  check(e::initialize_meta(register_movement_meta).ok());
  check(e::register_scene_component(operations()).ok());
  const auto id = [](std::uint8_t value) { return e::guid::from_bytes({89, value}); };
  e::prefab_description source;
  source.uuid = id(1);
  source.root_uuid = id(2);
  e::object_description root;
  root.uuid = id(2);
  root.name = "Source Root";
  source.objects.push_back(root);
  auto child = root;
  child.uuid = id(3);
  child.parent_uuid = root.uuid;
  child.components.set(movement_description{});
  child.components.set(*e::default_component_description(e::camera_description::type_guid));
  source.objects.push_back(child);
  e::scene_description scene;
  scene.uuid = id(4);
  e::object_description first;
  first.uuid = id(5);
  first.name = "Instance Root";
  first.transform.translation.x = 8;
  first.prefab.emplace();
  first.prefab->uri = "asset://prefabs/source.prefab.json";
  first.prefab->component_overrides.push_back(
      {root.uuid, movement_description::type_guid, movement_description{.speed = 4}});
  first.prefab->component_overrides.push_back({child.uuid, e::camera_description::type_guid, {}});
  first.prefab->overrides.push_back({.key = {.source_node_uuid = child.uuid,
                                             .type_guid = movement_description::type_guid,
                                             .property_id = e::meta::property_id{3},
                                             .path = {e::meta::property_id{1}}},
                                     .value = e::value{6.0F}});
  scene.objects.push_back(first);
  auto second = first;
  second.uuid = id(6);
  second.prefab->overrides.clear();
  second.prefab->component_overrides.clear();
  scene.objects.push_back(second);
  auto own = root;
  own.uuid = id(7);
  own.parent_uuid = first.uuid;
  scene.objects.push_back(own);
  const auto prefab_json = text(*e::encode_prefab(source));
  const auto scene_json = text(*e::encode_scene(scene));
  auto entries = ed::describe_prefab_overrides(*first.prefab, source);
  check(entries && entries->size() == 3);
  check((*entries)[0].label.find("acceleration") != std::string::npos);
  const auto decode_changes = [](const auto& changes) {
    const auto& json = *changes.front().replacement;
    return e::decode_scene(std::as_bytes(std::span{json.data(), json.size()}));
  };
  auto restore_all = ed::prepare_restore_prefab(scene_json, "scenes/main.scene.json", first.uuid);
  check(restore_all && restore_all->size() == 1);
  auto restored = decode_changes(*restore_all);
  check(restored && restored->objects[0].prefab->overrides.empty() &&
        restored->objects[0].prefab->component_overrides.empty());
  check(restored->objects[0].name == first.name &&
        restored->objects[0].transform.translation.x == 8);
  check(restored->objects[2].parent_uuid == first.uuid);
  for (const auto& entry : *entries) {
    auto single =
        ed::prepare_restore_prefab(scene_json, "scenes/main.scene.json", first.uuid, &entry);
    check(bool(single));
    auto decoded = decode_changes(*single);
    check(decoded && decoded->objects[0].prefab->overrides.size() == (entry.component ? 1U : 0U));
    check(decoded->objects[0].prefab->component_overrides.size() == (entry.component ? 1U : 2U));
    check(ed::prepare_restore_prefab(*single->front().replacement, "scenes/main.scene.json",
                                     first.uuid, &entry)
              .error() == e::error::not_found);
  }
  check(ed::prepare_restore_prefab(*restore_all->front().replacement, "scenes/main.scene.json",
                                   first.uuid)
            .error() == e::error::not_ready);
  check(ed::prepare_restore_prefab(scene_json, "scenes/main.scene.json", id(90)).error() ==
        e::error::not_found);
  ed::apply_prefab_author_request apply{.scene_path = "scenes/main.scene.json",
                                        .prefab_path = "prefabs/source.prefab.json",
                                        .prefab_uri = first.prefab->uri,
                                        .instance_uuid = first.uuid};
  const std::array mappings{ed::unpack_prefab_uuid_mapping{root.uuid, id(8)},
                            ed::unpack_prefab_uuid_mapping{child.uuid, id(9)}};
  ed::unpack_prefab_author_request unpack{.scene_path = apply.scene_path,
                                          .prefab_uri = apply.prefab_uri,
                                          .instance_uuid = first.uuid,
                                          .node_mappings = mappings};
  std::vector<ed::author_document_change> changes;
  e::scene_diagnostic diagnostic;
  check(ed::prepare_unpack_prefab(scene_json, prefab_json, unpack, changes, &diagnostic).ok());
  check(changes.size() == 1 && *changes[0].baseline == scene_json);
  const auto& unpacked_json = *changes[0].replacement;
  auto unpacked =
      e::decode_scene(std::as_bytes(std::span{unpacked_json.data(), unpacked_json.size()}));
  check(unpacked && unpacked->objects.size() == 4);
  auto find = [](auto& objects, e::guid uuid) -> auto& {
    auto found = std::ranges::find(objects, uuid, &e::object_description::uuid);
    check(found != objects.end());
    return *found;
  };
  const auto& unpacked_root = find(unpacked->objects, id(8));
  const auto& unpacked_child = find(unpacked->objects, id(9));
  check(!unpacked_root.prefab && unpacked_root.name == first.name &&
        unpacked_root.transform.translation.x == 8);
  check(unpacked_root.components.get<movement_description>()->speed == 4);
  check(unpacked_child.parent_uuid == id(8) &&
        !unpacked_child.components.get<e::camera_description>());
  check(unpacked_child.components.get<movement_description>()->settings.acceleration == 6);
  check(find(unpacked->objects, own.uuid).parent_uuid == id(8));
  check(find(unpacked->objects, second.uuid).prefab.has_value());
  ed::apply_prefab_author_plan plan;
  check(ed::prepare_apply_prefab(scene_json, prefab_json, apply, plan, &diagnostic).ok());
  check(plan.changes.size() == 2 && plan.affected_instance_uuids.size() == 2);
  auto applied = e::decode_prefab(std::as_bytes(
      std::span{plan.changes[0].replacement->data(), plan.changes[0].replacement->size()}));
  check(applied && applied->objects[0].components.get<movement_description>()->speed == 4);
  check(applied->objects[0].name == root.name && applied->objects[0].transform.translation.x == 0);
  check(!applied->objects[1].components.get<e::camera_description>() &&
        applied->objects[1].components.get<movement_description>()->settings.acceleration == 6);
  const auto successful_plan = plan;
  auto conflict = scene;
  conflict.objects[1].prefab->component_overrides.push_back(
      {root.uuid, movement_description::type_guid, movement_description{}});
  check(ed::prepare_apply_prefab(text(*e::encode_scene(conflict)), prefab_json, apply, plan,
                                 &diagnostic) == e::error::invalid_state);
  check(plan.changes.empty() && diagnostic.path.find(second.uuid.string()) != std::string::npos &&
        diagnostic.path.find(root.uuid.string()) != std::string::npos);
  conflict = scene;
  conflict.objects[1].prefab->overrides.push_back(
      {.key = {.source_node_uuid = child.uuid,
               .type_guid = e::camera_description::type_guid,
               .property_id = e::meta::property_id{1},
               .path = {e::meta::property_id{1}}},
       .value = e::value{0.8F}});
  check(ed::prepare_apply_prefab(text(*e::encode_scene(conflict)), prefab_json, apply, plan,
                                 &diagnostic) == e::error::not_found);
  check(plan.changes.empty() && diagnostic.path.find(second.uuid.string()) != std::string::npos);
  auto missing = source;
  missing.objects.pop_back();
  check(ed::prepare_unpack_prefab(scene_json, text(*e::encode_prefab(missing)), unpack, changes,
                                  &diagnostic)
            .failed() &&
        changes.empty());
  check(ed::prepare_unpack_prefab(scene_json, prefab_json, unpack, changes).ok());
  // 实际 World 的解包、快照撤销/重做、保存再加载。
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("gneiss-prefab-operations-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory / "scenes");
  std::filesystem::create_directories(directory / "prefabs");
  write(directory / "scenes/main.scene.json", scene_json);
  write(directory / "prefabs/source.prefab.json", prefab_json);
  author_scene editing;
  check(editing.open(directory, "asset://scenes/main.scene.json"));
  ed::scene_document_snapshot before, after;
  check(ed::capture_scene_document(*editing.application, editing.document, before).ok());
  // 缺少来源网格时仅返回依赖清单；默认快照准备仍拒绝，现有场景不变。
  auto needs_assets = before;
  auto mesh_source = std::make_shared<e::prefab_description>(source);
  mesh_source->objects[0].components.set(e::mesh_renderer_description{
      "asset://meshes/missing.gneiss-mesh", "asset://materials/missing.material.json"});
  needs_assets.description.prefabs.at(first.prefab->uri).resource =
      e::prefab_asset_ref{mesh_source};
  ed::scene_document_snapshot missing_snapshot;
  check(ed::prepare_scene_document(needs_assets, *restore_all, missing_snapshot) ==
        e::error::not_ready);
  std::vector<e::asset_source> missing_assets;
  check(ed::prepare_scene_document(needs_assets, *restore_all, missing_snapshot, &missing_assets)
            .ok());
  check(missing_assets.size() == 2 && missing_snapshot.assets.empty());
  check(editing.document.find_prefab_root(first.uuid));
  check(ed::prepare_scene_document(before, changes, after).ok());
  ed::editor_command_history history;
  check(history
            .execute({.label = "解包",
                      .undo =
                          [&] {
                            return ed::restore_scene_document(*editing.application,
                                                              editing.document, before);
                          },
                      .redo =
                          [&] {
                            return ed::restore_scene_document(*editing.application,
                                                              editing.document, after);
                          },
                      .merge_key = {}})
            .ok());
  check(editing.document.find_prefab_root(first.uuid) == nullptr &&
        editing.document.find_node(id(8)));
  check(history.undo().ok() && editing.document.find_prefab_root(first.uuid));
  check(history.redo().ok() && editing.document.find_node(id(8)));
  check(save_document(editing.document, directory).ok());
  check(history.clear().ok());
  editing.document.close();
  editing.application.reset();
  check(editing.open(directory, "asset://scenes/main.scene.json"));
  auto* loaded_root = editing.document.find_node(id(8));
  check(loaded_root);
  check(editing.document.components(loaded_root->node)->get<movement_description>()->speed == 4);
  auto* loaded_child = editing.document.find_node(id(9));
  check(loaded_child);
  check(!editing.document.components(loaded_child->node)->get<e::camera_description>());
  editing.document.close();
  editing.application.reset();
  // 整个实例恢复复用场景快照，撤销保留覆盖，重做及保存恢复来源组件。
  write(directory / "scenes/main.scene.json", scene_json);
  check(editing.open(directory, "asset://scenes/main.scene.json"));
  check(ed::capture_scene_document(*editing.application, editing.document, before).ok());
  check(ed::prepare_scene_document(before, *restore_all, after).ok());
  check(history
            .execute({.label = "恢复实例覆盖",
                      .undo =
                          [&] {
                            return ed::restore_scene_document(*editing.application,
                                                              editing.document, before);
                          },
                      .redo =
                          [&] {
                            return ed::restore_scene_document(*editing.application,
                                                              editing.document, after);
                          },
                      .merge_key = {}})
            .ok());
  const auto root_movement = [&] {
    return editing.document.components(editing.document.find_prefab_root(first.uuid)->node)
               ->get<movement_description>() != nullptr;
  };
  check(!root_movement());
  check(history.undo().ok() && root_movement());
  check(history.redo().ok() && !root_movement());
  check(save_document(editing.document, directory).ok());
  check(history.clear().ok());
  editing.document.close();
  editing.application.reset();
  check(editing.open(directory, "asset://scenes/main.scene.json"));
  check(!root_movement());
  auto* restored_child = editing.document.find_prefab_source(first.uuid, child.uuid);
  check(restored_child);
  auto restored_components = editing.document.components(restored_child->node);
  check(restored_components->get<e::camera_description>());
  check(restored_components->get<movement_description>()->settings.acceleration ==
        movement_description{}.settings.acceleration);
  editing.document.close();
  editing.application.reset();
  // Apply 独立保存来源；父场景仅变更内存，正常保存后重新加载两实例。
  write(directory / "scenes/main.scene.json", scene_json);
  check(editing.open(directory, "asset://scenes/main.scene.json"));
  check(editing.apply(successful_plan.changes, true));
  check(editing.document
            .capture_component_override(editing.document.find_prefab_root(first.uuid)->node,
                                        movement_description::type_guid)
            ->added == std::nullopt);
  check(editing.document.components(editing.document.find_prefab_root(second.uuid)->node)
            ->get<movement_description>()
            ->speed == 4);
  check(save_document(editing.document, directory).ok());
  editing.document.close();
  editing.application.reset();
  check(editing.open(directory, "asset://scenes/main.scene.json"));
  check(editing.document.components(editing.document.find_prefab_root(second.uuid)->node)
            ->get<movement_description>()
            ->speed == 4);
  editing.document.close();
  editing.application.reset();
  std::filesystem::remove_all(directory);
  return 0;
} catch (const std::exception& failure) {
  std::fprintf(stderr, "%s\n", failure.what());
  return 1;
}
