// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "native_author_transaction.h"
#include "prefab_authoring.h"

#include <gneiss/application.hpp>
#include <gneiss/scene.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr std::string_view scene_path = "scenes/gallery.scene.json";
constexpr std::string_view scene_uri = "asset://scenes/gallery.scene.json";
constexpr std::string_view lantern_prefab_path = "prefabs/lantern.prefab.json";
constexpr std::string_view lantern_prefab_uri = "asset://prefabs/lantern.prefab.json";
constexpr std::string_view pillar_uuid = "20000000-0000-4000-8000-000000000003";
constexpr std::string_view pillar_prefab_uuid = "22000000-0000-4000-8000-000000000000";
constexpr std::string_view pillar_instance_uuid = "22000000-0000-4000-8000-000000000001";
constexpr std::string_view left_instance_uuid = "20000000-0000-4000-8000-000000000010";
constexpr std::string_view center_instance_uuid = "20000000-0000-4000-8000-000000000020";

struct temporary_project final {
  std::filesystem::path root;

  ~temporary_project() { // NOLINT(bugprone-exception-escape): 错误码重载用于测试清理。
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
};

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] bool load_scene(const std::filesystem::path& asset_root,
                              std::uint64_t expected_objects, std::uint64_t expected_prefab_nodes) {
  const auto root = asset_root.generic_string();
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  gneiss::application application;
  gneiss::scene_instance scene;
  std::uint64_t objects = 0U;
  std::uint64_t prefab_nodes = 0U;
  return gneiss::application::create(desc, application) == gneiss::result::success &&
         gneiss::scene_instance::load(application.get(), scene_uri, scene) ==
             gneiss::result::success &&
         scene.get_node_count(objects) == gneiss::result::success && objects == expected_objects &&
         scene.get_prefab_node_count(prefab_nodes) == gneiss::result::success &&
         prefab_nodes == expected_prefab_nodes;
}

[[nodiscard]] bool
commit_inverse(const std::filesystem::path& asset_root,
               const std::vector<gneiss::editor::author_document_change>& changes) {
  std::vector<gneiss::editor::author_document_change> inverse;
  return gneiss::editor::invert_author_document_changes(changes, inverse) ==
             gneiss::result::success &&
         gneiss::editor::commit_native_author_transaction(asset_root, inverse) ==
             gneiss::result::success;
}

} // namespace

int main() try { // NOLINT(bugprone-exception-escape): 测试入口统一返回失败码。
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto project_root = std::filesystem::temp_directory_path() /
                            ("gneiss-lantern-prefab-authoring-" + std::to_string(suffix));
  const temporary_project cleanup{project_root};
  std::filesystem::copy(GNEISS_EDITOR_LANTERN_PROJECT, project_root,
                        std::filesystem::copy_options::recursive);
  const auto asset_root = project_root / "assets";
  const auto scene_file = asset_root / scene_path;
  const auto lantern_prefab_file = asset_root / lantern_prefab_path;

  if (!load_scene(asset_root, 7U, 15U)) {
    return 1;
  }

  const gneiss::editor::create_prefab_author_request create_request{
      .scene_path = scene_path,
      .prefab_path = "prefabs/authoring-pillar.prefab.json",
      .prefab_uri = "asset://prefabs/authoring-pillar.prefab.json",
      .root_uuid = pillar_uuid,
      .prefab_uuid = pillar_prefab_uuid,
      .instance_uuid = pillar_instance_uuid};
  std::vector<gneiss::editor::author_document_change> create_changes;
  if (gneiss::editor::prepare_create_prefab(read_text(scene_file), create_request,
                                            create_changes) != gneiss::result::success ||
      gneiss::editor::commit_native_author_transaction(asset_root, create_changes) !=
          gneiss::result::success ||
      !load_scene(asset_root, 6U, 17U)) {
    return 2;
  }

  const gneiss::editor::apply_prefab_author_request apply_request{
      .scene_path = scene_path,
      .prefab_path = lantern_prefab_path,
      .prefab_uri = lantern_prefab_uri,
      .instance_uuid = left_instance_uuid};
  gneiss::editor::apply_prefab_author_plan apply_plan;
  if (gneiss::editor::prepare_apply_prefab(read_text(scene_file), read_text(lantern_prefab_file),
                                           apply_request, apply_plan) != gneiss::result::success ||
      apply_plan.affected_instance_uuids.size() != 3U ||
      gneiss::editor::commit_native_author_transaction(asset_root, apply_plan.changes) !=
          gneiss::result::success ||
      !load_scene(asset_root, 6U, 17U)) {
    return 3;
  }

  constexpr std::array mappings{gneiss::editor::unpack_prefab_uuid_mapping{
                                    .source_node_uuid = "21000000-0000-4000-8000-000000000001",
                                    .target_node_uuid = "23000000-0000-4000-8000-000000000001"},
                                gneiss::editor::unpack_prefab_uuid_mapping{
                                    .source_node_uuid = "21000000-0000-4000-8000-000000000002",
                                    .target_node_uuid = "23000000-0000-4000-8000-000000000002"},
                                gneiss::editor::unpack_prefab_uuid_mapping{
                                    .source_node_uuid = "21000000-0000-4000-8000-000000000003",
                                    .target_node_uuid = "23000000-0000-4000-8000-000000000003"},
                                gneiss::editor::unpack_prefab_uuid_mapping{
                                    .source_node_uuid = "21000000-0000-4000-8000-000000000004",
                                    .target_node_uuid = "23000000-0000-4000-8000-000000000004"}};
  const gneiss::editor::unpack_prefab_author_request unpack_request{
      .scene_path = scene_path,
      .prefab_uri = lantern_prefab_uri,
      .instance_uuid = center_instance_uuid,
      .instance_root_uuid = "23000000-0000-4000-8000-000000000000",
      .node_mappings = mappings};
  std::vector<gneiss::editor::author_document_change> unpack_changes;
  if (gneiss::editor::prepare_unpack_prefab(read_text(scene_file), read_text(lantern_prefab_file),
                                            unpack_request,
                                            unpack_changes) != gneiss::result::success ||
      gneiss::editor::commit_native_author_transaction(asset_root, unpack_changes) !=
          gneiss::result::success ||
      !load_scene(asset_root, 11U, 12U)) {
    return 4;
  }

  if (!commit_inverse(asset_root, unpack_changes) || !load_scene(asset_root, 6U, 17U)) {
    return 5;
  }
  std::vector<gneiss::editor::author_document_change> unpack_redo;
  if (gneiss::editor::invert_author_document_changes(unpack_changes, unpack_redo) !=
          gneiss::result::success ||
      !commit_inverse(asset_root, unpack_redo) || !load_scene(asset_root, 11U, 12U)) {
    return 6;
  }
  return 0;
} catch (...) {
  return 99;
}
