// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_session.h"

#include <gneiss/application.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view scene_uri = "asset://scenes/triangle.scene.json";
constexpr std::string_view camera_uuid = "00000000-0000-4000-8000-000000000002";

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] bool write_text(const std::filesystem::path& path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(stream);
}

[[nodiscard]] gneiss::result create_application(const std::filesystem::path& root,
                                                gneiss::application& output) {
  const auto root_text = root.generic_string();
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = root_text.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root_text.size());
  return gneiss::application::create(desc, output);
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
  const auto version_end = source.find("\"version\": 2,");
  if (version_end == std::string::npos) {
    return 1;
  }
  source.insert(version_end + std::string_view{"\"version\": 2,"}.size(),
                "\n  \"editor_test_unknown\": 42,");
  if (!write_text(scene_path, source)) {
    return 2;
  }

  gneiss::application application;
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss::editor::editor_session session;
  if (create_application(root, application) != gneiss::result::success ||
      application.get_world(world) != gneiss::result::success ||
      session.open(application.get(), world, scene_uri) != gneiss::result::success ||
      session.select(session.nodes()[0].node) != gneiss::result::success) {
    return 3;
  }
  gneiss::transform transform = GNEISS_TRANSFORM_IDENTITY;
  transform.translation[0] = 7.0F;
  if (gneiss_world_entity_set_local_transform(world, session.selected_node()->entity.get(),
                                              &transform) != GNEISS_SUCCESS) {
    return 4;
  }
  session.mark_dirty();
  if (session.save(root) != gneiss::result::success || session.is_dirty()) {
    return 5;
  }
  const auto saved = read_text(scene_path);
  if (saved.find("\"editor_test_unknown\":42") == std::string::npos) {
    return 6;
  }

  transform.translation[0] = 9.0F;
  if (gneiss_world_entity_set_local_transform(world, session.selected_node()->entity.get(),
                                              &transform) != GNEISS_SUCCESS) {
    return 7;
  }
  session.mark_dirty();
  const auto blocker = root / "not-a-directory";
  if (!write_text(blocker, "blocker") || session.save(blocker) != gneiss::result::io ||
      !session.is_dirty() || read_text(scene_path) != saved) {
    return 8;
  }
  session.close();
  application.reset();

  gneiss::application reloaded_application;
  gneiss::scene_instance reloaded_scene;
  if (create_application(root, reloaded_application) != gneiss::result::success ||
      gneiss::scene_instance::load(reloaded_application.get(), scene_uri, reloaded_scene) !=
          gneiss::result::success) {
    return 9;
  }
  gneiss_world reloaded_world = GNEISS_NULL_WORLD;
  gneiss::scene_node_id camera_node;
  gneiss_entity_id camera_entity = GNEISS_NULL_ENTITY_ID;
  gneiss::transform reloaded_transform = GNEISS_TRANSFORM_IDENTITY;
  if (reloaded_application.get_world(reloaded_world) != gneiss::result::success ||
      reloaded_scene.find_node(camera_uuid, camera_node) != gneiss::result::success ||
      gneiss_scene_node_get_entity(reloaded_world, camera_node.get(), &camera_entity) !=
          GNEISS_SUCCESS ||
      gneiss_world_entity_get_local_transform(reloaded_world, camera_entity, &reloaded_transform) !=
          GNEISS_SUCCESS ||
      std::abs(reloaded_transform.translation[0] - 7.0F) > 0.0001F) {
    return 10;
  }
  reloaded_scene.reset();
  reloaded_application.reset();
  std::filesystem::remove_all(root);
  return 0;
} catch (...) {
  return 99;
}
