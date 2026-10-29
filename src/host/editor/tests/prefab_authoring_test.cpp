// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../../../tests/support/async_application.hpp"
#include "../../../../tests/support/async_editor_save.hpp"
#include "commands/prefab_authoring.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "runtime/application.hpp"

#include "engine/core/error.hpp"
#include "engine/core/math/transform.hpp"

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

using namespace gneiss;

namespace {

const gneiss::engine::guid root_uuid =
    *gneiss::engine::guid::parse("10000000-0000-4000-8000-000000000001");
const gneiss::engine::guid sibling_uuid =
    *gneiss::engine::guid::parse("10000000-0000-4000-8000-000000000003");
const gneiss::engine::guid prefab_uuid =
    *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000001");
const gneiss::engine::guid instance_uuid =
    *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000001");

constexpr std::string_view scene_json =
    R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","unknown_scene":"preserved","objects":[{"uuid":"10000000-0000-4000-8000-000000000001","name":"Lamp","parent":null,"transform":{"translation":[2,3,4],"rotation":[0,0,0,1],"scale":[2,2,2]},"components":[],"unknown_object":7},{"uuid":"10000000-0000-4000-8000-000000000002","name":"Shade","parent":"10000000-0000-4000-8000-000000000001","transform":{"translation":[0,1,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[]},{"uuid":"10000000-0000-4000-8000-000000000003","name":"Floor","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[]}]})";

constexpr std::string_view apply_prefab_json = R"({
  "format":"gneiss.prefab","version":1,
  "prefab_uuid":"20000000-0000-4000-8000-000000000001","root_uuid":"10000000-0000-4000-8000-000000000001",
  "objects":[
    {"uuid":"10000000-0000-4000-8000-000000000001","name":"Lamp","parent":null,
     "transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
     "components":[]},
    {"uuid":"10000000-0000-4000-8000-000000000002","name":"Shade",
     "parent":"10000000-0000-4000-8000-000000000001",
     "transform":{"translation":[0,1,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
     "components":[],"unknown_source":true}
  ]
})";

constexpr std::string_view apply_scene_json =
    R"({"format":"gneiss.scene","version":5,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"name":"First","parent":null,"prefab":{"uri":"asset://prefabs/lamp.prefab.json","overrides":[{"source_node_uuid":"10000000-0000-4000-8000-000000000002","type_guid":"69644f20b2d24e488c7491f4f952ec2d","property_id":1,"value":{"kind":"vec3","value":[7,8,9]}}]},"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"uuid":"30000000-0000-4000-8000-000000000001"},{"name":"Second","parent":null,"prefab":{"uri":"asset://prefabs/lamp.prefab.json","overrides":[{"source_node_uuid":"10000000-0000-4000-8000-000000000002","type_guid":"69644f20b2d24e488c7491f4f952ec2d","property_id":3,"value":{"kind":"vec3","value":[3,3,3]}}]},"transform":{"translation":[4,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"uuid":"30000000-0000-4000-8000-000000000002"}],"unknown_scene":9})";

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
  auto* instances = yyjson_obj_get(scene_root, "objects");
  auto* instance = yyjson_arr_get(instances, 1U);
  auto* instance_translation = yyjson_obj_get(yyjson_obj_get(instance, "transform"), "translation");
  return yyjson_arr_size(prefab_objects) == 2U && yyjson_arr_size(scene_objects) == 2U &&
         yyjson_arr_size(instances) == 2U &&
         yyjson_get_num(yyjson_arr_get(source_translation, 0U)) == 0.0 &&
         yyjson_get_num(yyjson_arr_get(instance_translation, 0U)) == 2.0 &&
         yyjson_obj_get(source_root, "unknown_object") != nullptr &&
         yyjson_obj_get(scene_root, "unknown_scene") != nullptr &&
         yyjson_obj_get(prefab_root, "unknown_scene") == nullptr;
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
  auto* instances = yyjson_obj_get(scene_root, "objects");
  auto* first_overrides =
      yyjson_obj_get(yyjson_obj_get(yyjson_arr_get(instances, 0U), "prefab"), "overrides");
  auto* second_overrides =
      yyjson_obj_get(yyjson_obj_get(yyjson_arr_get(instances, 1U), "prefab"), "overrides");
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

  auto* unpacked_root = yyjson_arr_get(objects, 1U);
  auto* source_root = yyjson_arr_get(objects, 1U);
  auto* source_child = yyjson_arr_get(objects, 2U);
  auto* const source_root_parent = yyjson_obj_get(source_root, "parent");
  auto* const source_child_parent = yyjson_obj_get(source_child, "parent");
  auto* translation = yyjson_obj_get(yyjson_obj_get(source_child, "transform"), "translation");
  return yyjson_arr_size(objects) == 3U &&
         yyjson_obj_get(yyjson_arr_get(objects, 0U), "prefab") != nullptr &&
         std::string_view{yyjson_get_str(yyjson_obj_get(unpacked_root, "uuid"))} ==
             "50000000-0000-4000-8000-000000000002" &&
         yyjson_is_null(source_root_parent) &&
         std::string_view{yyjson_get_str(source_child_parent)} ==
             "50000000-0000-4000-8000-000000000002" &&
         yyjson_get_num(yyjson_arr_get(translation, 0U)) == 7.0 &&
         yyjson_obj_get(source_child, "unknown_source") != nullptr &&
         yyjson_obj_get(root, "unknown_scene") != nullptr;
}

[[nodiscard]] bool
create_rejections_are_valid(const gneiss::editor::create_prefab_author_request& request,
                            std::vector<gneiss::editor::author_document_change>& changes) {
  auto nested_scene = std::string(scene_json);
  nested_scene.insert(
      nested_scene.rfind(']'),
      R"(,{"uuid":"40000000-0000-4000-8000-000000000001","name":"Nested","parent":"10000000-0000-4000-8000-000000000002","prefab":{"uri":"asset://prefabs/other.prefab.json","overrides":[]},"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}})");
  if (gneiss::editor::prepare_create_prefab(nested_scene, request, changes) !=
          gneiss::engine::error::unsupported ||
      !changes.empty()) {
    return false;
  }

  auto missing = request;
  missing.root_uuid = *gneiss::engine::guid::parse("50000000-0000-4000-8000-000000000001");
  if (gneiss::editor::prepare_create_prefab(scene_json, missing, changes) !=
          gneiss::engine::error::not_found ||
      !changes.empty()) {
    return false;
  }
  auto mismatched = request;
  mismatched.prefab_uri = "asset://prefabs/other.prefab.json";
  if (gneiss::editor::prepare_create_prefab(scene_json, mismatched, changes) !=
          gneiss::engine::error::invalid_argument ||
      !changes.empty()) {
    return false;
  }
  auto duplicate = request;
  duplicate.instance_uuid = sibling_uuid;
  return gneiss::editor::prepare_create_prefab(scene_json, duplicate, changes) ==
             gneiss::engine::error::invalid_argument &&
         changes.empty();
}

} // namespace

int main() try {
  if (gneiss::engine::initialize_meta().failed())
    return 99;
  const gneiss::editor::create_prefab_author_request request{
      .scene_path = "scenes/main.scene.json",
      .prefab_path = "prefabs/lamp.prefab.json",
      .prefab_uri = "asset://prefabs/lamp.prefab.json",
      .root_uuid = root_uuid,
      .prefab_uuid = prefab_uuid,
      .instance_uuid = instance_uuid};
  std::vector<gneiss::editor::author_document_change> changes;
  if (gneiss::editor::prepare_create_prefab(scene_json, request, changes) !=
          gneiss::engine::error::success ||
      changes.size() != 2U || !verify_documents(changes)) {
    return 1;
  }

  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-prefab-authoring-test-" + std::to_string(suffix));
  std::filesystem::create_directories(root / "scenes");
  std::filesystem::create_directories(root / "prefabs");
  gneiss::test_support::author_scene editing;
  if (!write_text(root / "scenes" / "main.scene.json", scene_json) ||
      !editing.open(root, "asset://scenes/main.scene.json") || !editing.apply(changes, true) ||
      read_text(root / "scenes" / "main.scene.json") != scene_json ||
      read_text(root / "prefabs" / "lamp.prefab.json") != *changes[0].replacement ||
      !editing.counts(1U, 2U))
    return 2;
  const auto created_source = read_text(root / "prefabs" / "lamp.prefab.json");
  if (!editing.restore(editing.before) || !editing.counts(3U, 0U) ||
      read_text(root / "prefabs" / "lamp.prefab.json") != created_source ||
      read_text(root / "scenes" / "main.scene.json") != scene_json ||
      !editing.restore(editing.after) || !editing.counts(1U, 2U))
    return 3;
  editing.document.close();
  editing.application.reset();

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
                                           apply_plan) != gneiss::engine::error::success ||
      !verify_apply_plan(apply_plan) ||
      !write_text(root / "scenes" / "main.scene.json", apply_scene_json) ||
      !write_text(root / "prefabs" / "lamp.prefab.json", apply_prefab_json) ||
      !editing.open(root, "asset://scenes/main.scene.json") ||
      !editing.apply(apply_plan.changes, true) ||
      read_text(root / "scenes" / "main.scene.json") != apply_scene_json ||
      !editing.counts(0U, 4U)) {
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
                                           apply_plan) != gneiss::engine::error::not_ready ||
      !apply_plan.changes.empty()) {
    return 10;
  }

  editing.document.close();
  editing.application.reset();
  const std::array mappings{
      gneiss::editor::unpack_prefab_uuid_mapping{
          .source_node_uuid = root_uuid,
          .target_node_uuid = *gneiss::engine::guid::parse("50000000-0000-4000-8000-000000000002")},
      gneiss::editor::unpack_prefab_uuid_mapping{
          .source_node_uuid = *gneiss::engine::guid::parse("10000000-0000-4000-8000-000000000002"),
          .target_node_uuid =
              *gneiss::engine::guid::parse("50000000-0000-4000-8000-000000000003")}};
  const gneiss::editor::unpack_prefab_author_request unpack_request{
      .scene_path = "scenes/main.scene.json",
      .prefab_uri = "asset://prefabs/lamp.prefab.json",
      .instance_uuid = instance_uuid,
      .node_mappings = mappings};
  std::vector<gneiss::editor::author_document_change> unpack_changes;
  if (gneiss::editor::prepare_unpack_prefab(apply_scene_json, apply_prefab_json, unpack_request,
                                            unpack_changes) != gneiss::engine::error::success ||
      !verify_unpack_change(unpack_changes) ||
      !write_text(root / "scenes" / "main.scene.json", apply_scene_json) ||
      !write_text(root / "prefabs" / "lamp.prefab.json", apply_prefab_json) ||
      !editing.open(root, "asset://scenes/main.scene.json") ||
      !editing.apply(unpack_changes, false) || !editing.counts(2U, 2U)) {
    return 11;
  }
  if (!editing.restore(editing.before) || !editing.counts(0U, 4U) ||
      read_text(root / "scenes" / "main.scene.json") != apply_scene_json ||
      read_text(root / "prefabs" / "lamp.prefab.json") != apply_prefab_json ||
      !editing.restore(editing.after) || !editing.counts(2U, 2U))
    return 12;
  // 解包改变根 UUID 时，自有子对象必须跟随新根，不能留下悬空父引用。
  auto attached_scene = std::string(apply_scene_json);
  attached_scene.insert(
      attached_scene.rfind(']'),
      R"(,{"uuid":"60000000-0000-4000-8000-000000000001","name":"Own child","parent":"30000000-0000-4000-8000-000000000001","transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[]})");
  if (gneiss::editor::prepare_unpack_prefab(attached_scene, apply_prefab_json, unpack_request,
                                            unpack_changes)
          .failed())
    return 14;
  const auto& attached_output = *unpack_changes.front().replacement;
  document_ptr attached_document{yyjson_read(attached_output.data(), attached_output.size(), 0)};
  auto* attached_objects = yyjson_obj_get(yyjson_doc_get_root(attached_document.get()), "objects");
  auto* attached_child = yyjson_arr_get(attached_objects, 1U);
  auto* attached_parent = yyjson_obj_get(attached_child, "parent");
  if (!yyjson_is_str(attached_parent) ||
      std::string_view(yyjson_get_str(attached_parent)) != mappings[0].target_node_uuid.string())
    return 14;

  auto invalid_unpack = unpack_request;
  auto duplicate_mappings = mappings;
  duplicate_mappings[1].target_node_uuid = mappings[0].target_node_uuid;
  invalid_unpack.node_mappings = duplicate_mappings;
  if (gneiss::editor::prepare_unpack_prefab(apply_scene_json, apply_prefab_json, invalid_unpack,
                                            unpack_changes) !=
          gneiss::engine::error::invalid_argument ||
      !unpack_changes.empty()) {
    return 13;
  }
  editing.document.close();
  editing.application.reset();
  std::filesystem::remove_all(root);
  return 0;
} catch (...) {
  return 99;
}
