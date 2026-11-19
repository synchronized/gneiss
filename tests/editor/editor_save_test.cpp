// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_document.hpp"
#include "../support/async_editor_save.hpp"
#include "../support/async_mesh_edit.hpp"
#include "../support/scene_query.hpp"
#include "../support/value_result.hpp"
#include "documents/scene_edit_session.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/guid.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/function/scene/scene_types.hpp>

#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/scene/world.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

using namespace gneiss;

namespace {

constexpr std::string_view scene_uri = "asset://scenes/triangle.scene.json";
const gneiss::guid camera_uuid = *gneiss::guid::parse("00000000-0000-4000-8000-000000000002");
constexpr std::string_view mesh_uri = "asset://models/triangle.mesh.json";
constexpr std::string_view material_uri = "asset://materials/triangle.material.json";

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] bool write_text(const std::filesystem::path& path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(stream);
}

[[nodiscard]] gneiss::error
create_application(const std::filesystem::path& root,
                   std::shared_ptr<gneiss::runtime::application>& output) {
  const auto root_text = root.generic_string();
  gneiss::runtime::application_configuration desc;
  desc.executor = &gneiss::test_support::application_tasks();
  desc.asset_root = root_text;
  return gneiss::test_support::assign_value(gneiss::runtime::application::create(desc), output);
}

} // namespace

int main() try {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-editor-save-test-" + std::to_string(suffix));
  std::filesystem::copy(GNEISS_EDITOR_TEST_ASSET_ROOT, root,
                        std::filesystem::copy_options::recursive);
  const auto scene_path = root / "scenes" / "triangle.scene.json";
  auto source = read_text(scene_path);
  const auto version_end = source.find("\"version\": 5,");
  if (version_end == std::string::npos) {
    return 1;
  }
  source.insert(version_end + std::string_view{"\"version\": 5,"}.size(),
                "\n  \"editor_test_unknown\": 42,");
  if (!write_text(scene_path, source)) {
    return 2;
  }

  std::shared_ptr<gneiss::runtime::application> application;
  gneiss::world* world = nullptr;
  gneiss::editor::scene_edit_session session;
  if (create_application(root, application) != gneiss::error::success ||
      (world = application->native_world()) == nullptr ||
      gneiss::test_support::open_document(session, *application, world, scene_uri) !=
          gneiss::error::success ||
      session.select(gneiss::test_support::scene_query(session).nodes[0].node) !=
          gneiss::error::success) {
    return 3;
  }
  gneiss::math::transform transform{};
  transform.translation[0] = 7.0F;
  if (session.set_local_transform(session.selected_node()->node, transform) !=
      gneiss::error::success) {
    return 4;
  }
  session.mark_dirty();
  gneiss::entity_id created_node;
  if (gneiss::test_support::create_mesh_document(*application, session, "Created", mesh_uri,
                                                 material_uri,
                                                 created_node) != gneiss::error::success ||
      !created_node.is_valid() || gneiss::test_support::scene_query(session).nodes.size() != 3U ||
      session.set_mesh_component(created_node, mesh_uri, material_uri) != gneiss::error::success) {
    return 5;
  }
  const auto created_uuid = session.selected_node()->uuid;
  gneiss::entity_id empty_parent;
  gneiss::entity_id empty_child;
  if (session.create_node("Empty Parent", {}, empty_parent) != gneiss::error::success ||
      session.create_node("Empty Child", empty_parent, empty_child) != gneiss::error::success ||
      session.rename_node(empty_child, "Renamed Child") != gneiss::error::success ||
      session.reparent_node(created_node, empty_parent) != gneiss::error::success) {
    return 13;
  }
  const auto child_uuid = session.find_node(session.selected_node()->uuid)->uuid;
  gneiss::camera_description author_camera{{1.04719755F, 0.1F, 1000.0F}, false};
  if (session.set_camera(empty_child, author_camera) != gneiss::error::success ||
      session.find_node(child_uuid)->components.get<gneiss::camera_description>() == nullptr ||
      session.remove_camera(session.find_node(child_uuid)->node) != gneiss::error::success ||
      session.find_node(child_uuid)->components.get<gneiss::camera_description>() != nullptr ||
      session.remove_mesh_component(session.find_node(created_uuid)->node) !=
          gneiss::error::success ||
      session.set_mesh_component(session.find_node(created_uuid)->node, mesh_uri, material_uri) !=
          gneiss::error::success) {
    return 16;
  }
  const auto parent_uuid = session.find_node(session.selected_node()->uuid)->uuid;
  gneiss::editor::scene_subtree_snapshot subtree;
  if (session.destroy_subtree(empty_parent, subtree) != gneiss::error::success ||
      session.find_node(parent_uuid).ok() ||
      session.restore_subtree(subtree, empty_parent) != gneiss::error::success ||
      session.find_node(parent_uuid).failed()) {
    return 14;
  }
  gneiss::entity_id duplicate;
  gneiss::editor::scene_subtree_snapshot duplicate_snapshot;
  if (session.duplicate_subtree(empty_parent, {}, duplicate) != gneiss::error::success ||
      duplicate == empty_parent || gneiss::test_support::scene_query(session).nodes.size() != 8U ||
      session.destroy_subtree(duplicate, duplicate_snapshot) != gneiss::error::success ||
      gneiss::test_support::scene_query(session).nodes.size() != 5U) {
    return 15;
  }
  created_node = session.find_node(created_uuid)->node;
  // 运行时名称变化不得进入文档子树快照。
  application->native_world()->find(created_node).set_name("Live snapshot");
  gneiss::editor::scene_subtree_snapshot snapshot;
  if (session.destroy_subtree(created_node, snapshot) != gneiss::error::success ||
      snapshot.description()->objects.size() != 1U ||
      snapshot.description()->objects.front().name != "Created" ||
      session.find_node(created_uuid).ok() ||
      gneiss::test_support::scene_query(session).nodes.size() != 4U ||
      session.restore_subtree(snapshot, created_node) != gneiss::error::success ||
      session.find_node(created_uuid).failed() ||
      gneiss::test_support::scene_query(session).nodes.size() != 5U) {
    return 6;
  }
  if (gneiss::test_support::save_document(session, root) != gneiss::error::success ||
      session.is_dirty()) {
    return 7;
  }
  const auto saved = read_text(scene_path);
  if (saved.find("\"editor_test_unknown\":42") != std::string::npos ||
      saved.find(created_uuid.string()) == std::string::npos ||
      saved.find(mesh_uri) == std::string::npos) {
    return 8;
  }

  transform.translation[0] = 9.0F;
  if (session.set_local_transform(session.selected_node()->node, transform) !=
      gneiss::error::success) {
    return 9;
  }
  session.mark_dirty();
  const auto blocker = root / "not-a-directory";
  if (!write_text(blocker, "blocker") ||
      gneiss::test_support::save_document(session, blocker).ok() || !session.is_dirty() ||
      read_text(scene_path) != saved) {
    return 10;
  }
  const gneiss::guid new_scene_uuid = *gneiss::guid::parse("00000000-0000-4000-8000-000000000099");
  constexpr std::string_view new_scene_uri = "asset://scenes/new.scene.json";
  std::string converted_uri;
  if (gneiss::editor::make_asset_uri(root, root / "scenes" / "new.scene.json", converted_uri) !=
          gneiss::error::success ||
      converted_uri != new_scene_uri ||
      gneiss::editor::make_asset_uri(root, root.parent_path() / "escaped.scene.json",
                                     converted_uri) != gneiss::error::invalid_argument) {
    return 18;
  }
  gneiss::entity_id new_node;
  if (session.create_empty(*application, world, new_scene_uuid) != gneiss::error::success ||
      !session.is_open() || !session.is_dirty() || !session.uri().empty() ||
      session.create_node("Root", {}, new_node) != gneiss::error::success ||
      gneiss::test_support::save_document(session, root) != gneiss::error::invalid_argument ||
      gneiss::test_support::save_document(session, root, nullptr, new_scene_uri) !=
          gneiss::error::success ||
      session.is_dirty() || session.uri() != new_scene_uri ||
      gneiss::test_support::save_document(session, root, nullptr, new_scene_uri) !=
          gneiss::error::invalid_state ||
      read_text(root / "scenes" / "new.scene.json").find(new_scene_uuid.string()) ==
          std::string::npos) {
    return 17;
  }
  session.close();
  application.reset();

  std::shared_ptr<gneiss::runtime::application> reloaded_application;
  std::vector<gneiss::scene_object_binding> reloaded_objects;
  gneiss::scene_instance reloaded_scene{};
  if (create_application(root, reloaded_application) != gneiss::error::success ||
      gneiss::test_support::load_scene(*reloaded_application, scene_uri, &reloaded_scene, nullptr,
                                       false, &reloaded_objects) != gneiss::error::success) {
    return 11;
  }
  const auto camera_binding = std::ranges::find_if(reloaded_objects, [&](const auto& binding) {
    return binding.object == camera_uuid && !binding.source;
  });
  const auto created_binding = std::ranges::find_if(reloaded_objects, [&](const auto& binding) {
    return binding.object == created_uuid && !binding.source;
  });
  if (camera_binding == reloaded_objects.end() || created_binding == reloaded_objects.end())
    return 12;
  const auto camera = reloaded_application->native_world()->object(camera_binding->entity);
  const auto created = reloaded_application->native_world()->object(created_binding->entity);
  if (!camera || !created || std::abs(camera->local_transform().translation[0] - 7.0F) > 0.0001F)
    return 12;
  reloaded_scene.unload();
  if (camera->is_valid() || created->is_valid() || reloaded_scene.is_valid())
    return 12;
  reloaded_application.reset();
  std::filesystem::remove_all(root);
  return 0;
} catch (...) {
  return 99;
}
