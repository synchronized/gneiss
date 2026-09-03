// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "native_author_transaction.h"
#include "prefab_authoring.h"

#include <gneiss/application.hpp>
#include <gneiss/scene.hpp>

#include <yyjson.h>

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

[[nodiscard]] bool load_created_scene(const std::filesystem::path& root) {
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
         scene.get_node_count(object_count) == gneiss::result::success && object_count == 1U &&
         scene.get_prefab_node_count(prefab_node_count) == gneiss::result::success &&
         prefab_node_count == 3U;
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
      !load_created_scene(root)) {
    return 2;
  }

  std::vector<gneiss::editor::author_document_change> undo;
  if (gneiss::editor::invert_author_document_changes(changes, undo) != gneiss::result::success ||
      gneiss::editor::commit_native_author_transaction(root, undo) != gneiss::result::success ||
      read_text(root / "scenes" / "main.scene.json") != scene_json ||
      std::filesystem::exists(root / "prefabs" / "lamp.prefab.json")) {
    return 3;
  }

  auto nested_scene = std::string(scene_json);
  nested_scene.replace(
      nested_scene.rfind("[]"), 2U,
      R"([{"instance_uuid":"40000000-0000-4000-8000-000000000001","name":"Nested","parent":"10000000-0000-4000-8000-000000000002","prefab":"asset://prefabs/other.prefab.json","transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"overrides":[]}])");
  if (gneiss::editor::prepare_create_prefab(nested_scene, request, changes) !=
          gneiss::result::unsupported ||
      !changes.empty()) {
    return 4;
  }

  auto missing = request;
  missing.root_uuid = "50000000-0000-4000-8000-000000000001";
  if (gneiss::editor::prepare_create_prefab(scene_json, missing, changes) !=
          gneiss::result::not_found ||
      !changes.empty()) {
    return 5;
  }
  auto mismatched = request;
  mismatched.prefab_uri = "asset://prefabs/other.prefab.json";
  if (gneiss::editor::prepare_create_prefab(scene_json, mismatched, changes) !=
          gneiss::result::invalid_argument ||
      !changes.empty()) {
    return 6;
  }
  auto duplicate = request;
  duplicate.instance_uuid = sibling_uuid;
  if (gneiss::editor::prepare_create_prefab(scene_json, duplicate, changes) !=
          gneiss::result::invalid_argument ||
      !changes.empty()) {
    return 7;
  }
  std::filesystem::remove_all(root);
  return 0;
} catch (...) {
  return 99;
}
