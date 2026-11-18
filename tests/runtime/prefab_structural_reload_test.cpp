// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_application.hpp"
#include "../support/async_scene_reload.hpp"
#include "../support/value_result.hpp"
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/base/guid.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/scene/instantiation.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/resource/asset_file.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view prefab_uri = "asset://prefabs/test.prefab.json";
const gneiss::guid source_root_uuid =
    *gneiss::guid::parse("30000000-0000-4000-8000-000000000002");
const gneiss::guid source_child_uuid =
    *gneiss::guid::parse("30000000-0000-4000-8000-000000000003");
const gneiss::guid first_instance_uuid =
    *gneiss::guid::parse("30000000-0000-4000-8000-000000000012");
const gneiss::guid second_instance_uuid =
    *gneiss::guid::parse("30000000-0000-4000-8000-000000000013");

void write_prefab(const std::filesystem::path& path, gneiss::guid root_uuid,
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

gneiss::error find_source(const std::shared_ptr<gneiss::runtime::application>& application,
                          gneiss::scene_instance scene,
                          const std::vector<gneiss::scene_object_binding>& objects,
                          gneiss::guid instance_uuid, gneiss::guid source_uuid,
                          gneiss::game_object& output) {
  const auto binding = std::ranges::find_if(objects, [&](const auto& object) {
    return object.object == instance_uuid && object.source == source_uuid;
  });
  if (binding == objects.end())
    return gneiss::error::not_found;
  output = application->native_world()->find(binding->entity);
  if (!output)
    return gneiss::error::not_found;
  return scene.contains(output) ? gneiss::error::success : gneiss::error::invalid_handle;
}

} // namespace

int main() try {
  const auto root = std::filesystem::temp_directory_path() / "gneiss-prefab-structural-reload-test";
  std::filesystem::remove_all(root);
  std::filesystem::copy(GNEISS_TEST_ASSET_ROOT, root, std::filesystem::copy_options::recursive);
  const auto root_text = root.generic_string();
  gneiss::runtime::application_configuration config;
  config.asset_root = root_text;
  gneiss::tasks::task_scheduler scheduler({.mode = gneiss::tasks::execution_mode::cooperative});
  std::shared_ptr<gneiss::runtime::application> application;
  gneiss::scene_instance scene = {};
  constexpr std::string_view scene_uri = "asset://scenes/prefab.scene.json";
  if (gneiss::test_support::assign_value(gneiss::runtime::application::create(config), application)
          .failed())
    return 1;
  gneiss::scene_content prepared;
  gneiss::scene_resource_bindings assets;
  if (gneiss::test_support::prepare_scene(*application, scene_uri, prepared, assets, &scheduler)
          .failed())
    return 1;
  auto description = *prepared.description;
  gneiss::game_object_description second;
  second.uuid = second_instance_uuid;
  second.prefab = gneiss::prefab_reference_description{std::string(prefab_uri), {}, {}};
  description.objects.push_back(second);
  auto bytes = gneiss::asset_file::serialize_bytes(description);
  if (!bytes)
    return 2;
  {
    std::ofstream output(root / "scenes" / "prefab.scene.json", std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes->data()),
                 static_cast<std::streamsize>(bytes->size()));
  }
  // 释放准备阶段的弱缓存所有者，正式激活重新读取更新后的场景。
  prepared = {};
  gneiss::host::scene_open_result loaded;
  if (gneiss::test_support::activate_scene_for_test(*application, scheduler, scene_uri, loaded)
          .failed())
    return 2;
  scene = loaded.scene;
  auto objects = loaded.objects;

  gneiss::game_object first_before;
  gneiss::game_object second_before;
  if (find_source(application, scene, objects, first_instance_uuid, source_root_uuid,
                  first_before) != gneiss::error::success ||
      find_source(application, scene, objects, second_instance_uuid, source_root_uuid,
                  second_before) != gneiss::error::success) {
    return 3;
  }

  const auto prefab_path = root / "prefabs" / "test.prefab.json";
  write_prefab(prefab_path, source_root_uuid, true);
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, loaded.reload_state,
                                                  prefab_uri, true,
                                                  &objects) != gneiss::error::success) {
    return 4;
  }
  gneiss::game_object first_after;
  gneiss::game_object second_after;
  gneiss::game_object child;

  if (find_source(application, scene, objects, first_instance_uuid, source_root_uuid,
                  first_after) != gneiss::error::success ||
      find_source(application, scene, objects, second_instance_uuid, source_root_uuid,
                  second_after) != gneiss::error::success ||
      first_after != first_before || second_after != second_before ||
      first_after.name() != "Reloaded Root" || second_after.name() != "Reloaded Root" ||
      first_after.local_transform().translation[0] != 3.0F ||
      second_after.local_transform().translation[0] != 2.0F ||
      find_source(application, scene, objects, first_instance_uuid, source_child_uuid, child) !=
          gneiss::error::success ||
      find_source(application, scene, objects, second_instance_uuid, source_child_uuid, child) !=
          gneiss::error::success ||
      std::ranges::count_if(objects, [](const auto& item) { return bool(item.source); }) != 6U) {
    return 5;
  }

  const gneiss::guid replacement_root_uuid =
      *gneiss::guid::parse("30000000-0000-4000-8000-000000000099");
  write_prefab(prefab_path, replacement_root_uuid, false);
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, loaded.reload_state,
                                                  prefab_uri, true,
                                                  &objects) != gneiss::error::success ||
      find_source(application, scene, objects, first_instance_uuid, source_root_uuid,
                  first_after) != gneiss::error::not_found ||
      find_source(application, scene, objects, first_instance_uuid, replacement_root_uuid,
                  first_after) != gneiss::error::success ||
      find_source(application, scene, objects, second_instance_uuid, replacement_root_uuid,
                  second_after) != gneiss::error::success) {
    return 6;
  }
  // 来源目标重新出现后，原覆盖自动恢复；冲突由组合层报告，运行查询不缓存。
  write_prefab(prefab_path, source_root_uuid, false);
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, loaded.reload_state,
                                                  prefab_uri, true, &objects)
          .failed() ||
      find_source(application, scene, objects, first_instance_uuid, source_root_uuid, first_after)
          .failed() ||
      first_after.local_transform().translation.x != 3)
    return 8;

  scene.unload();
  const bool destroyed = application->shutdown() == gneiss::error::success;
  std::filesystem::remove_all(root);
  return destroyed ? 0 : 7;
} catch (...) {
  return 99;
}
