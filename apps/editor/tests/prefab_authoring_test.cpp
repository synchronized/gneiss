// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "native_author_transaction.h"
#include "prefab_authoring.h"

#include <gneiss/application.hpp>
#include <gneiss/scene.hpp>

#include <yyjson.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view root_uuid = "10000000-0000-4000-8000-000000000001";
constexpr std::string_view sibling_uuid = "10000000-0000-4000-8000-000000000003";
constexpr std::string_view prefab_uuid = "20000000-0000-4000-8000-000000000001";
constexpr std::string_view instance_uuid = "30000000-0000-4000-8000-000000000001";

constexpr std::string_view scene_json = R"({
  "format":"gneiss.scene","version":4,
  "scene_uuid":"00000000-0000-4000-8000-000000000001",
  "unknown_scene":"preserved",
  "objects":[
    {"uuid":"10000000-0000-4000-8000-000000000001","name":"Lamp","parent":null,
     "transform":{"translation":[2,3,4],"rotation":[0,0,0,1],"scale":[2,2,2]},
     "components":{},"unknown_object":7},
    {"uuid":"10000000-0000-4000-8000-000000000002","name":"Shade",
     "parent":"10000000-0000-4000-8000-000000000001",
     "transform":{"translation":[0,1,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
     "components":{}},
    {"uuid":"10000000-0000-4000-8000-000000000003","name":"Floor","parent":null,
     "transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
     "components":{}}
  ],"prefab_instances":[]
})";

constexpr std::string_view apply_prefab_json = R"({
  "format":"gneiss.prefab","version":1,
  "prefab_uuid":"20000000-0000-4000-8000-000000000001",
  "objects":[
    {"uuid":"10000000-0000-4000-8000-000000000001","name":"Lamp","parent":null,
     "transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
     "components":{}},
    {"uuid":"10000000-0000-4000-8000-000000000002","name":"Shade",
     "parent":"10000000-0000-4000-8000-000000000001",
     "transform":{"translation":[0,1,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
     "components":{},"unknown_source":true}
  ]
})";

constexpr std::string_view apply_scene_json = R"({
  "format":"gneiss.scene","version":4,
  "scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[],
  "prefab_instances":[
    {"instance_uuid":"30000000-0000-4000-8000-000000000001","name":"First","parent":null,
     "prefab":"asset://prefabs/lamp.prefab.json",
     "transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
     "overrides":[{"source_node_uuid":"10000000-0000-4000-8000-000000000002",
       "type_id":"69644f20b2d24e488c7491f4f952ec2d","field_id":1,
       "value":{"kind":"vec3","value":[7,8,9]}}]},
    {"instance_uuid":"30000000-0000-4000-8000-000000000002","name":"Second","parent":null,
     "prefab":"asset://prefabs/lamp.prefab.json",
     "transform":{"translation":[4,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
     "overrides":[{"source_node_uuid":"10000000-0000-4000-8000-000000000002",
       "type_id":"69644f20b2d24e488c7491f4f952ec2d","field_id":3,
       "value":{"kind":"vec3","value":[3,3,3]}}]}
  ],"unknown_scene":9
})";

struct document_deleter final {
  void operator()(yyjson_doc* document) const noexcept { yyjson_doc_free(document); }
};

using document_ptr = std::unique_ptr<yyjson_doc, document_deleter>;

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] bool write_text(const std::filesystem::path& path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(stream);
}

[[nodiscard]] const gneiss::editor::author_document_change*
find_change(const std::vector<gneiss::editor::author_document_change>& changes,
            std::string_view path) {
  const auto found =
      std::ranges::find(changes, path, &gneiss::editor::author_document_change::path);
  return found == changes.end() ? nullptr : &*found;
}

[[nodiscard]] bool
verify_documents(const std::vector<gneiss::editor::author_document_change>& changes) {
  const auto* prefab = find_change(changes, "prefabs/lamp.prefab.json");
  const auto* scene = find_change(changes, "scenes/main.scene.json");
  if (prefab == nullptr || scene == nullptr || prefab->baseline.has_value() ||
      !prefab->replacement || !scene->replacement) {
    return false;
  }
  document_ptr prefab_document{
      yyjson_read(prefab->replacement->data(), prefab->replacement->size(), YYJSON_READ_NOFLAG)};
  document_ptr scene_document{
      yyjson_read(scene->replacement->data(), scene->replacement->size(), YYJSON_READ_NOFLAG)};
  auto* prefab_root = prefab_document ? yyjson_doc_get_root(prefab_document.get()) : nullptr;
  auto* prefab_objects = yyjson_obj_get(prefab_root, "objects");
  auto* source_root = yyjson_arr_get(prefab_objects, 0U);
  auto* source_translation =
      yyjson_obj_get(yyjson_obj_get(source_root, "transform"), "translation");
  auto* scene_root = scene_document ? yyjson_doc_get_root(scene_document.get()) : nullptr;
  auto* scene_objects = yyjson_obj_get(scene_root, "objects");
  auto* instances = yyjson_obj_get(scene_root, "prefab_instances");
  auto* instance = yyjson_arr_get(instances, 0U);
  auto* instance_translation = yyjson_obj_get(yyjson_obj_get(instance, "transform"), "translation");
  return yyjson_arr_size(prefab_objects) == 2U && yyjson_arr_size(scene_objects) == 1U &&
         yyjson_arr_size(instances) == 1U &&
         yyjson_get_num(yyjson_arr_get(source_translation, 0U)) == 0.0 &&
         yyjson_get_num(yyjson_arr_get(instance_translation, 0U)) == 2.0 &&
         yyjson_obj_get(source_root, "unknown_object") != nullptr &&
         yyjson_obj_get(scene_root, "unknown_scene") != nullptr &&
         yyjson_obj_get(prefab_root, "unknown_scene") == nullptr;
}

[[nodiscard]] bool load_scene(const std::filesystem::path& root, std::uint64_t expected_objects,
                              std::uint64_t expected_prefab_nodes) {
  const auto root_text = root.generic_string();
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = root_text.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root_text.size());
  gneiss::application application;
  gneiss::scene_instance scene;
  std::uint64_t object_count = 0U;
  std::uint64_t prefab_node_count = 0U;
  return gneiss::application::create(desc, application) == gneiss::result::success &&
         gneiss::scene_instance::load(application.get(), "asset://scenes/main.scene.json", scene) ==
             gneiss::result::success &&
         scene.get_node_count(object_count) == gneiss::result::success &&
         object_count == expected_objects &&
         scene.get_prefab_node_count(prefab_node_count) == gneiss::result::success &&
         prefab_node_count == expected_prefab_nodes;
}

[[nodiscard]] bool verify_apply_plan(const gneiss::editor::apply_prefab_author_plan& plan) {
  if (plan.changes.size() != 2U || plan.affected_instance_uuids.size() != 2U ||
      plan.affected_instance_uuids[0] != instance_uuid) {
    return false;
  }
  const auto* prefab = find_change(plan.changes, "prefabs/lamp.prefab.json");
  const auto* scene = find_change(plan.changes, "scenes/main.scene.json");
  if (prefab == nullptr || scene == nullptr || !prefab->replacement || !scene->replacement) {
    return false;
  }
  document_ptr prefab_document{
      yyjson_read(prefab->replacement->data(), prefab->replacement->size(), YYJSON_READ_NOFLAG)};
  document_ptr scene_document{
      yyjson_read(scene->replacement->data(), scene->replacement->size(), YYJSON_READ_NOFLAG)};
  auto* prefab_root = prefab_document ? yyjson_doc_get_root(prefab_document.get()) : nullptr;
  auto* source = yyjson_arr_get(yyjson_obj_get(prefab_root, "objects"), 1U);
  auto* translation = yyjson_obj_get(yyjson_obj_get(source, "transform"), "translation");
  auto* scene_root = scene_document ? yyjson_doc_get_root(scene_document.get()) : nullptr;
  auto* instances = yyjson_obj_get(scene_root, "prefab_instances");
  auto* first_overrides = yyjson_obj_get(yyjson_arr_get(instances, 0U), "overrides");
  auto* second_overrides = yyjson_obj_get(yyjson_arr_get(instances, 1U), "overrides");
  return yyjson_get_num(yyjson_arr_get(translation, 0U)) == 7.0 &&
         yyjson_arr_size(first_overrides) == 0U && yyjson_arr_size(second_overrides) == 1U &&
         yyjson_obj_get(source, "unknown_source") != nullptr &&
         yyjson_obj_get(scene_root, "unknown_scene") != nullptr;
}

[[nodiscard]] bool
verify_unpack_change(const std::vector<gneiss::editor::author_document_change>& changes) {
  const auto* scene = find_change(changes, "scenes/main.scene.json");
  if (changes.size() != 1U || scene == nullptr || !scene->replacement) {
    return false;
  }
  document_ptr document{
      yyjson_read(scene->replacement->data(), scene->replacement->size(), YYJSON_READ_NOFLAG)};
  auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
  auto* objects = yyjson_obj_get(root, "objects");
  auto* instances = yyjson_obj_get(root, "prefab_instances");
  auto* unpacked_root = yyjson_arr_get(objects, 0U);
  auto* source_root = yyjson_arr_get(objects, 1U);
  auto* source_child = yyjson_arr_get(objects, 2U);
  auto* const source_root_parent = yyjson_obj_get(source_root, "parent");
  auto* const source_child_parent = yyjson_obj_get(source_child, "parent");
  auto* translation = yyjson_obj_get(yyjson_obj_get(source_child, "transform"), "translation");
  return yyjson_arr_size(objects) == 3U && yyjson_arr_size(instances) == 1U &&
         std::string_view{yyjson_get_str(yyjson_obj_get(unpacked_root, "uuid"))} ==
             "50000000-0000-4000-8000-000000000001" &&
         std::string_view{yyjson_get_str(source_root_parent)} ==
             "50000000-0000-4000-8000-000000000001" &&
         std::string_view{yyjson_get_str(source_child_parent)} ==
             "50000000-0000-4000-8000-000000000002" &&
         yyjson_get_num(yyjson_arr_get(translation, 0U)) == 7.0 &&
         yyjson_obj_get(source_child, "unknown_source") != nullptr &&
         yyjson_obj_get(root, "unknown_scene") != nullptr;
}

[[nodiscard]] bool create_rejections_are_valid(
    const gneiss::editor::create_prefab_author_request& request,
    std::vector<gneiss::editor::author_document_change>& changes) {
  auto nested_scene = std::string(scene_json);
  nested_scene.replace(
      nested_scene.rfind("[]"), 2U,
      R"([{"instance_uuid":"40000000-0000-4000-8000-000000000001","name":"Nested","parent":"10000000-0000-4000-8000-000000000002","prefab":"asset://prefabs/other.prefab.json","transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"overrides":[]}])");
  if (gneiss::editor::prepare_create_prefab(nested_scene, request, changes) !=
          gneiss::result::unsupported ||
      !changes.empty()) {
    return false;
  }

  auto missing = request;
  missing.root_uuid = "50000000-0000-4000-8000-000000000001";
  if (gneiss::editor::prepare_create_prefab(scene_json, missing, changes) !=
          gneiss::result::not_found ||
      !changes.empty()) {
    return false;
  }
  auto mismatched = request;
  mismatched.prefab_uri = "asset://prefabs/other.prefab.json";
  if (gneiss::editor::prepare_create_prefab(scene_json, mismatched, changes) !=
          gneiss::result::invalid_argument ||
      !changes.empty()) {
    return false;
  }
  auto duplicate = request;
  duplicate.instance_uuid = sibling_uuid;
  return gneiss::editor::prepare_create_prefab(scene_json, duplicate, changes) ==
             gneiss::result::invalid_argument &&
         changes.empty();
}

} // namespace

int main() try {
  const gneiss::editor::create_prefab_author_request request{
      .scene_path = "scenes/main.scene.json",
      .prefab_path = "prefabs/lamp.prefab.json",
      .prefab_uri = "asset://prefabs/lamp.prefab.json",
      .root_uuid = root_uuid,
      .prefab_uuid = prefab_uuid,
      .instance_uuid = instance_uuid};
  std::vector<gneiss::editor::author_document_change> changes;
  if (gneiss::editor::prepare_create_prefab(scene_json, request, changes) !=
          gneiss::result::success ||
      changes.size() != 2U || !verify_documents(changes)) {
    return 1;
  }

  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-prefab-authoring-test-" + std::to_string(suffix));
  std::filesystem::create_directories(root / "scenes");
  std::filesystem::create_directories(root / "prefabs");
  if (!write_text(root / "scenes" / "main.scene.json", scene_json) ||
      gneiss::editor::commit_native_author_transaction(root, changes) != gneiss::result::success ||
      read_text(root / "scenes" / "main.scene.json") != *changes[1].replacement ||
      read_text(root / "prefabs" / "lamp.prefab.json") != *changes[0].replacement ||
      !load_scene(root, 1U, 3U)) {
    return 2;
  }

  std::vector<gneiss::editor::author_document_change> undo;
  if (gneiss::editor::invert_author_document_changes(changes, undo) != gneiss::result::success ||
      gneiss::editor::commit_native_author_transaction(root, undo) != gneiss::result::success ||
      read_text(root / "scenes" / "main.scene.json") != scene_json ||
      std::filesystem::exists(root / "prefabs" / "lamp.prefab.json")) {
    return 3;
  }

  if (!create_rejections_are_valid(request, changes)) {
    return 4;
  }

  const gneiss::editor::apply_prefab_author_request apply_request{
      .scene_path = "scenes/main.scene.json",
      .prefab_path = "prefabs/lamp.prefab.json",
      .prefab_uri = "asset://prefabs/lamp.prefab.json",
      .instance_uuid = instance_uuid};
  gneiss::editor::apply_prefab_author_plan apply_plan;
  if (gneiss::editor::prepare_apply_prefab(apply_scene_json, apply_prefab_json, apply_request,
                                           apply_plan) != gneiss::result::success ||
      !verify_apply_plan(apply_plan) ||
      !write_text(root / "scenes" / "main.scene.json", apply_scene_json) ||
      !write_text(root / "prefabs" / "lamp.prefab.json", apply_prefab_json) ||
      gneiss::editor::commit_native_author_transaction(root, apply_plan.changes) !=
          gneiss::result::success ||
      !load_scene(root, 0U, 6U)) {
    return 8;
  }
  auto empty_overrides = std::string(apply_scene_json);
  const auto override_start = empty_overrides.find(R"("overrides":[{)");
  const auto override_end = empty_overrides.find("}]", override_start);
  if (override_start == std::string::npos || override_end == std::string::npos) {
    return 9;
  }
  empty_overrides.replace(override_start, override_end + 2U - override_start, R"("overrides":[])");
  if (gneiss::editor::prepare_apply_prefab(empty_overrides, apply_prefab_json, apply_request,
                                           apply_plan) != gneiss::result::not_ready ||
      !apply_plan.changes.empty()) {
    return 10;
  }

  const std::array mappings{gneiss::editor::unpack_prefab_uuid_mapping{
                                .source_node_uuid = root_uuid,
                                .target_node_uuid = "50000000-0000-4000-8000-000000000002"},
                            gneiss::editor::unpack_prefab_uuid_mapping{
                                .source_node_uuid = "10000000-0000-4000-8000-000000000002",
                                .target_node_uuid = "50000000-0000-4000-8000-000000000003"}};
  const gneiss::editor::unpack_prefab_author_request unpack_request{
      .scene_path = "scenes/main.scene.json",
      .prefab_uri = "asset://prefabs/lamp.prefab.json",
      .instance_uuid = instance_uuid,
      .instance_root_uuid = "50000000-0000-4000-8000-000000000001",
      .node_mappings = mappings};
  std::vector<gneiss::editor::author_document_change> unpack_changes;
  if (gneiss::editor::prepare_unpack_prefab(apply_scene_json, apply_prefab_json, unpack_request,
                                            unpack_changes) != gneiss::result::success ||
      !verify_unpack_change(unpack_changes) ||
      !write_text(root / "scenes" / "main.scene.json", apply_scene_json) ||
      !write_text(root / "prefabs" / "lamp.prefab.json", apply_prefab_json) ||
      gneiss::editor::commit_native_author_transaction(root, unpack_changes) !=
          gneiss::result::success ||
      !load_scene(root, 3U, 3U)) {
    return 11;
  }
  std::vector<gneiss::editor::author_document_change> unpack_undo;
  if (gneiss::editor::invert_author_document_changes(unpack_changes, unpack_undo) !=
          gneiss::result::success ||
      gneiss::editor::commit_native_author_transaction(root, unpack_undo) !=
          gneiss::result::success ||
      read_text(root / "scenes" / "main.scene.json") != apply_scene_json ||
      !load_scene(root, 0U, 6U)) {
    return 12;
  }
  auto invalid_unpack = unpack_request;
  invalid_unpack.instance_root_uuid = mappings[0].target_node_uuid;
  if (gneiss::editor::prepare_unpack_prefab(apply_scene_json, apply_prefab_json, invalid_unpack,
                                            unpack_changes) != gneiss::result::invalid_argument ||
      !unpack_changes.empty()) {
    return 13;
  }
  std::filesystem::remove_all(root);
  return 0;
} catch (...) {
  return 99;
}
