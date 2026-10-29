// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../../../tests/support/async_application.hpp"
#include "../../../../tests/support/async_editor_save.hpp"
#include "commands/prefab_authoring.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "runtime/application.hpp"

#include "engine/core/error.hpp"
#include "engine/core/math/transform.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace gneiss;

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

} // namespace

int main() try { // NOLINT(bugprone-exception-escape): 测试入口统一返回失败码。
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto project_root = std::filesystem::temp_directory_path() /
                            ("gneiss-lantern-prefab-authoring-" + std::to_string(suffix));
  const temporary_project cleanup{project_root};
  std::filesystem::copy(GNEISS_EDITOR_LANTERN_PROJECT, project_root,
                        std::filesystem::copy_options::recursive);
  const auto asset_root = project_root / "assets";
  const auto lantern_prefab_file = asset_root / lantern_prefab_path;

  gneiss::test_support::author_scene editing;
  if (!editing.open(asset_root, scene_uri) || !editing.counts(7U, 12U)) {
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
  if (gneiss::editor::prepare_create_prefab(editing.content(), create_request, create_changes) !=
          gneiss::engine::error::success ||
      !editing.apply(create_changes, true) || !editing.counts(6U, 13U)) {
    return 2;
  }

  const gneiss::editor::apply_prefab_author_request apply_request{
      .scene_path = scene_path,
      .prefab_path = lantern_prefab_path,
      .prefab_uri = lantern_prefab_uri,
      .instance_uuid = left_instance_uuid};
  gneiss::editor::apply_prefab_author_plan apply_plan;
  if (gneiss::editor::prepare_apply_prefab(editing.content(), read_text(lantern_prefab_file),
                                           apply_request,
                                           apply_plan) != gneiss::engine::error::success ||
      apply_plan.affected_instance_uuids.size() != 3U || !editing.apply(apply_plan.changes, true) ||
      !editing.counts(6U, 13U)) {
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
      .node_mappings = mappings};
  std::vector<gneiss::editor::author_document_change> unpack_changes;
  if (gneiss::editor::prepare_unpack_prefab(editing.content(), read_text(lantern_prefab_file),
                                            unpack_request,
                                            unpack_changes) != gneiss::engine::error::success ||
      !editing.apply(unpack_changes, false) || !editing.counts(10U, 9U)) {
    return 4;
  }

  if (!editing.restore(editing.before) || !editing.counts(6U, 13U)) {
    return 5;
  }
  if (!editing.restore(editing.after) || !editing.counts(10U, 9U))
    return 6;
  return 0;
} catch (...) {
  return 99;
}
