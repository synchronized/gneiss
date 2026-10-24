// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../../../tests/support/c_owner.hpp"
#include "documents/scene_document.hpp"
#include "engine/core/result.hpp"
#include "runtime/application_registry.hpp"

#include "engine/values/core/result.hpp"
#include "engine/values/render.hpp"
#include "engine/values/scene.hpp"
#include <gneiss/engine/application.h>

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
constexpr std::string_view camera_uuid = "00000000-0000-4000-8000-000000000002";
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

[[nodiscard]] gneiss::engine::result
create_application(const std::filesystem::path& root,
                   gneiss::test_support::application_owner& output) {
  const auto root_text = root.generic_string();
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = root_text.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root_text.size());
  return gneiss::engine::result{gneiss_application_create(&desc, output.put())};
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
  const auto version_end = source.find("\"version\": 4,");
  if (version_end == std::string::npos) {
    return 1;
  }
  source.insert(version_end + std::string_view{"\"version\": 4,"}.size(),
                "\n  \"editor_test_unknown\": 42,");
  if (!write_text(scene_path, source)) {
    return 2;
  }

  gneiss::test_support::application_owner application;
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss::editor::scene_document session;
  if (create_application(root, application) != gneiss::engine::result::success ||
      gneiss::engine::result{gneiss_application_get_world(application.get(), &world)} !=
          gneiss::engine::result::success ||
      session.open(*gneiss::runtime::find_application(application.get()), world, scene_uri) !=
          gneiss::engine::result::success ||
      session.select(session.nodes()[0].node) != gneiss::engine::result::success) {
    return 3;
  }
  gneiss::engine::transform transform{};
  transform.translation[0] = 7.0F;
  if (([&](entity_id entity, const gneiss::engine::transform& value) {
        const auto native = to_native(value);
        return gneiss::engine::result{
            gneiss_world_entity_set_local_transform(world, entity.get(), &native)};
      })(gneiss::entity_id{session.selected_node()->entity.get()}, transform) !=
      gneiss::engine::result::success) {
    return 4;
  }
  session.mark_dirty();
  gneiss::scene_node_id created_node;
  if (session.create_mesh_renderer_node("Created", mesh_uri, material_uri, created_node) !=
          gneiss::engine::result::success ||
      !created_node.is_valid() || session.nodes().size() != 3U ||
      session.set_mesh_renderer(created_node, mesh_uri, material_uri) !=
          gneiss::engine::result::success) {
    return 5;
  }
  const auto created_uuid = session.selected_node()->uuid;
  gneiss::scene_node_id empty_parent;
  gneiss::scene_node_id empty_child;
  if (session.create_node("Empty Parent", {}, empty_parent) != gneiss::engine::result::success ||
      session.create_node("Empty Child", empty_parent, empty_child) !=
          gneiss::engine::result::success ||
      session.rename_node(empty_child, "Renamed Child") != gneiss::engine::result::success ||
      session.reparent_node(created_node, empty_parent) != gneiss::engine::result::success) {
    return 13;
  }
  const auto child_uuid = session.find_node(session.selected_node()->uuid)->uuid;
  gneiss::engine::camera_description author_camera{1.04719755F, 0.1F, 1000.0F, false};
  if (session.set_camera(empty_child, author_camera) != gneiss::engine::result::success ||
      (session.find_node(child_uuid)->component_flags & GNEISS_SCENE_NODE_COMPONENT_CAMERA) == 0U ||
      session.remove_camera(session.find_node(child_uuid)->node) !=
          gneiss::engine::result::success ||
      (session.find_node(child_uuid)->component_flags & GNEISS_SCENE_NODE_COMPONENT_CAMERA) != 0U ||
      session.remove_mesh_renderer(session.find_node(created_uuid)->node) !=
          gneiss::engine::result::success ||
      session.set_mesh_renderer(session.find_node(created_uuid)->node, mesh_uri, material_uri) !=
          gneiss::engine::result::success) {
    return 16;
  }
  const auto parent_uuid = session.find_node(session.selected_node()->uuid)->uuid;
  gneiss::editor::scene_subtree_snapshot subtree;
  if (session.destroy_subtree(empty_parent, subtree) != gneiss::engine::result::success ||
      session.find_node(parent_uuid) != nullptr ||
      session.restore_subtree(subtree, empty_parent) != gneiss::engine::result::success ||
      session.find_node(parent_uuid) == nullptr) {
    return 14;
  }
  gneiss::scene_node_id duplicate;
  gneiss::editor::scene_subtree_snapshot duplicate_snapshot;
  if (session.duplicate_subtree(empty_parent, {}, duplicate) != gneiss::engine::result::success ||
      duplicate == empty_parent || session.nodes().size() != 8U ||
      session.destroy_subtree(duplicate, duplicate_snapshot) != gneiss::engine::result::success ||
      session.nodes().size() != 5U) {
    return 15;
  }
  created_node = session.find_node(created_uuid)->node;
  gneiss::editor::scene_node_snapshot snapshot;
  if (session.destroy_node(created_node, snapshot) != gneiss::engine::result::success ||
      session.find_node(created_uuid) != nullptr || session.nodes().size() != 4U ||
      session.restore_mesh_renderer_node(snapshot, created_node) !=
          gneiss::engine::result::success ||
      session.find_node(created_uuid) == nullptr || session.nodes().size() != 5U) {
    return 6;
  }
  if (session.save(root) != gneiss::engine::result::success || session.is_dirty()) {
    return 7;
  }
  const auto saved = read_text(scene_path);
  if (saved.find("\"editor_test_unknown\":42") == std::string::npos ||
      saved.find(created_uuid) == std::string::npos || saved.find(mesh_uri) == std::string::npos) {
    return 8;
  }

  transform.translation[0] = 9.0F;
  if (([&](entity_id entity, const gneiss::engine::transform& value) {
        const auto native = to_native(value);
        return gneiss::engine::result{
            gneiss_world_entity_set_local_transform(world, entity.get(), &native)};
      })(gneiss::entity_id{session.selected_node()->entity.get()}, transform) !=
      gneiss::engine::result::success) {
    return 9;
  }
  session.mark_dirty();
  const auto blocker = root / "not-a-directory";
  if (!write_text(blocker, "blocker") || session.save(blocker) != gneiss::engine::result::io ||
      !session.is_dirty() || read_text(scene_path) != saved) {
    return 10;
  }
  constexpr std::string_view new_scene_uuid = "00000000-0000-4000-8000-000000000099";
  constexpr std::string_view new_scene_uri = "asset://scenes/new.scene.json";
  std::string converted_uri;
  if (gneiss::editor::make_asset_uri(root, root / "scenes" / "new.scene.json", converted_uri) !=
          gneiss::engine::result::success ||
      converted_uri != new_scene_uri ||
      gneiss::editor::make_asset_uri(root, root.parent_path() / "escaped.scene.json",
                                     converted_uri) != gneiss::engine::result::invalid_argument) {
    return 18;
  }
  gneiss::scene_node_id new_node;
  if (session.create_empty(*gneiss::runtime::find_application(application.get()), world,
                           new_scene_uuid) != gneiss::engine::result::success ||
      !session.is_open() || !session.is_dirty() || !session.uri().empty() ||
      session.create_node("Root", {}, new_node) != gneiss::engine::result::success ||
      session.save(root) != gneiss::engine::result::invalid_argument ||
      session.save_as(root, new_scene_uri) != gneiss::engine::result::success ||
      session.is_dirty() || session.uri() != new_scene_uri ||
      session.save_as(root, new_scene_uri) != gneiss::engine::result::invalid_state ||
      read_text(root / "scenes" / "new.scene.json").find(new_scene_uuid) == std::string::npos) {
    return 17;
  }
  session.close();
  application.reset();

  gneiss::test_support::application_owner reloaded_application;
  gneiss::test_support::scene_owner reloaded_scene;
  if (create_application(root, reloaded_application) != gneiss::engine::result::success ||
      ([&] {
        const std::string_view uri = scene_uri;
        return gneiss::engine::result{
            gneiss_scene_instance_load(reloaded_application.get(), uri.data(), uri.size(),
                                       reloaded_scene.put(reloaded_application.get()))};
      })() != gneiss::engine::result::success) {
    return 11;
  }
  gneiss_world reloaded_world = GNEISS_NULL_WORLD;
  gneiss::scene_node_id camera_node;
  gneiss::scene_node_id reloaded_created_node;
  gneiss_entity_id camera_entity = GNEISS_NULL_ENTITY_ID;
  gneiss::engine::transform reloaded_transform{};
  if (gneiss::engine::result{gneiss_application_get_world(
          reloaded_application.get(), &reloaded_world)} != gneiss::engine::result::success ||
      ([&](std::string_view uuid, scene_node_id& out_node) {
        gneiss_scene_node_id node = GNEISS_NULL_SCENE_NODE_ID;
        const auto native_result = gneiss::engine::result{gneiss_scene_instance_find_node(
            reloaded_scene.owner(), reloaded_scene.get(), uuid.data(), uuid.size(), &node)};
        if (native_result == gneiss::engine::result::success) {
          out_node = scene_node_id{node};
        }
        return native_result;
      })(camera_uuid, camera_node) != gneiss::engine::result::success ||
      ([&](std::string_view uuid, scene_node_id& out_node) {
        gneiss_scene_node_id node = GNEISS_NULL_SCENE_NODE_ID;
        const auto native_result = gneiss::engine::result{gneiss_scene_instance_find_node(
            reloaded_scene.owner(), reloaded_scene.get(), uuid.data(), uuid.size(), &node)};
        if (native_result == gneiss::engine::result::success) {
          out_node = scene_node_id{node};
        }
        return native_result;
      })(created_uuid, reloaded_created_node) != gneiss::engine::result::success ||
      gneiss::engine::result{gneiss_scene_node_get_entity(
          reloaded_world, camera_node.get(), &camera_entity)} != gneiss::engine::result::success ||
      ([&](entity_id entity, gneiss::engine::transform& output) {
        gneiss_transform native{};
        const auto status = gneiss::engine::result{
            gneiss_world_entity_get_local_transform(reloaded_world, entity.get(), &native)};
        if (status.ok()) {
          output = from_native(native);
        }
        return status;
      })(gneiss::entity_id{camera_entity}, reloaded_transform) != gneiss::engine::result::success ||
      std::abs(reloaded_transform.translation[0] - 7.0F) > 0.0001F) {
    return 12;
  }
  reloaded_scene.reset();
  reloaded_application.reset();
  std::filesystem::remove_all(root);
  return 0;
} catch (...) {
  return 99;
}
