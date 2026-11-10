// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_application.hpp"
#include "../support/async_scene_reload.hpp"
#include "../support/scene_access.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/scene/scene_instantiation.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/asset_file.hpp"
#include "runtime/application.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view prefab_uri = "asset://prefabs/test.prefab.json";
const gneiss::engine::guid source_root_uuid =
    *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000002");
const gneiss::engine::guid source_child_uuid =
    *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000003");
const gneiss::engine::guid first_instance_uuid =
    *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000012");
const gneiss::engine::guid second_instance_uuid =
    *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000013");

void write_prefab(const std::filesystem::path& path, gneiss::engine::guid root_uuid,
                  bool add_child) {
  std::string json =
      std::string{
          R"({"format":"gneiss.prefab","version":1,"prefab_uuid":"30000000-0000-4000-8000-000000000001","root_uuid":"30000000-0000-4000-8000-000000000004","objects":[{"uuid":"30000000-0000-4000-8000-000000000004","parent":{"object":null,"source":null},"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[]},{"uuid":")"} +
      root_uuid.string() +
      R"(","name":"Reloaded Root","parent":{"object":"30000000-0000-4000-8000-000000000004","source":null},"transform":{"translation":[2,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[]})";
  if (add_child) {
    json +=
        std::string{R"(,{"uuid":")"} + source_child_uuid.string() +
        R"(","name":"Reloaded Child","parent":{"object":")" + root_uuid.string() +
        R"(","source":null},"transform":{"translation":[0,1,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[]})";
  }
  json += "]}";
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(json.data(), static_cast<std::streamsize>(json.size()));
}

gneiss::engine::error find_source(const std::shared_ptr<gneiss::runtime::application>& application,
                                  gneiss::scene_instance_id scene,
                                  const std::vector<gneiss::engine::scene_object_binding>& objects,
                                  gneiss::engine::guid instance_uuid,
                                  gneiss::engine::guid source_uuid,
                                  gneiss::engine::scene_node_info& output) {
  const auto binding = std::ranges::find_if(objects, [&](const auto& object) {
    return object.object == instance_uuid && object.source == source_uuid;
  });
  if (binding == objects.end())
    return gneiss::engine::error::not_found;
  auto count = gneiss::test_support::scene_access{*application}->get_node_count(scene);
  if (!count)
    return count.error();
  for (std::size_t index = 0; index < *count; ++index) {
    auto info = gneiss::test_support::scene_access{*application}->get_node_info(scene, index);
    if (!info)
      return info.error();
    if (info->node == binding->node) {
      output = *info;
      return gneiss::engine::error::success;
    }
  }
  return gneiss::engine::error::not_found;
}

} // namespace

int main() try {
  const auto root = std::filesystem::temp_directory_path() / "gneiss-prefab-structural-reload-test";
  std::filesystem::remove_all(root);
  std::filesystem::copy(GNEISS_TEST_ASSET_ROOT, root, std::filesystem::copy_options::recursive);
  const auto root_text = root.generic_string();
  gneiss::runtime::application_configuration config;
  config.asset_root = root_text;
  gneiss::engine::task_scheduler scheduler({.mode = gneiss::engine::execution_mode::cooperative});
  std::shared_ptr<gneiss::runtime::application> application;
  gneiss::scene_instance_id scene = {};
  constexpr std::string_view scene_uri = "asset://scenes/prefab.scene.json";
  if (gneiss::test_support::assign_value(gneiss::runtime::application::create(config), application)
          .failed())
    return 1;
  gneiss::engine::scene_instantiation_data prepared;
  gneiss::engine::scene_resource_bindings assets;
  if (gneiss::test_support::prepare_scene(*application, scene_uri, prepared, assets, &scheduler)
          .failed())
    return 1;
  auto description = prepared.description;
  gneiss::engine::game_object_description second;
  second.uuid = second_instance_uuid;
  second.prefab = gneiss::engine::prefab_reference_description{std::string(prefab_uri), {}, {}};
  description.objects.push_back(second);
  auto bytes = gneiss::engine::asset_file::serialize_bytes(description);
  if (!bytes)
    return 2;
  {
    std::ofstream output(root / "scenes" / "prefab.scene.json", std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes->data()),
                 static_cast<std::streamsize>(bytes->size()));
  }
  // 释放准备阶段的弱缓存所有者，正式激活重新读取更新后的场景。
  prepared = {};
  gneiss::runtime::scene_load_completion loaded;
  if (gneiss::test_support::activate_scene_for_test(*application, scheduler, scene_uri, loaded)
          .failed())
    return 2;
  scene = loaded.scene;
  auto objects = loaded.objects;

  gneiss::engine::scene_node_info first_before;
  gneiss::engine::scene_node_info second_before;
  if (find_source(application, scene, objects, first_instance_uuid, source_root_uuid,
                  first_before) != gneiss::engine::error::success ||
      find_source(application, scene, objects, second_instance_uuid, source_root_uuid,
                  second_before) != gneiss::engine::error::success) {
    return 3;
  }

  const auto prefab_path = root / "prefabs" / "test.prefab.json";
  write_prefab(prefab_path, source_root_uuid, true);
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, scene, prefab_uri, true,
                                                  &objects) != gneiss::engine::error::success) {
    return 4;
  }
  gneiss::engine::scene_node_info first_after;
  gneiss::engine::scene_node_info second_after;
  gneiss::engine::scene_node_info child;

  if (find_source(application, scene, objects, first_instance_uuid, source_root_uuid,
                  first_after) != gneiss::engine::error::success ||
      find_source(application, scene, objects, second_instance_uuid, source_root_uuid,
                  second_after) != gneiss::engine::error::success ||
      first_after.node != first_before.node || first_after.entity != first_before.entity ||
      second_after.node != second_before.node || second_after.entity != second_before.entity ||
      first_after.name != "Reloaded Root" || second_after.name != "Reloaded Root" ||
      first_after.local_transform.translation[0] != 3.0F ||
      second_after.local_transform.translation[0] != 2.0F ||
      find_source(application, scene, objects, first_instance_uuid, source_child_uuid, child) !=
          gneiss::engine::error::success ||
      find_source(application, scene, objects, second_instance_uuid, source_child_uuid, child) !=
          gneiss::engine::error::success ||
      std::ranges::count_if(objects, [](const auto& item) { return bool(item.source); }) != 6U) {
    return 5;
  }

  const gneiss::engine::guid replacement_root_uuid =
      *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000099");
  write_prefab(prefab_path, replacement_root_uuid, false);
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, scene, prefab_uri, true,
                                                  &objects) != gneiss::engine::error::success ||
      find_source(application, scene, objects, first_instance_uuid, source_root_uuid,
                  first_after) != gneiss::engine::error::not_found ||
      find_source(application, scene, objects, first_instance_uuid, replacement_root_uuid,
                  first_after) != gneiss::engine::error::success ||
      find_source(application, scene, objects, second_instance_uuid, replacement_root_uuid,
                  second_after) != gneiss::engine::error::success) {
    return 6;
  }
  // 来源目标重新出现后，原覆盖自动恢复；冲突由组合层报告，运行查询不缓存。
  write_prefab(prefab_path, source_root_uuid, false);
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, scene, prefab_uri, true,
                                                  &objects)
          .failed() ||
      find_source(application, scene, objects, first_instance_uuid, source_root_uuid, first_after)
          .failed() ||
      first_after.local_transform.translation.x != 3)
    return 8;

  const bool destroyed = gneiss::test_support::scene_access{*application}->unload(scene) ==
                             gneiss::engine::error::success &&
                         application->shutdown() == gneiss::engine::error::success;
  std::filesystem::remove_all(root);
  return destroyed ? 0 : 7;
} catch (...) {
  return 99;
}
