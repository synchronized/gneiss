// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/file_system.h"
#include "asset/resource_cache.h"
#include "asset/virtual_file_system.h"
#include "render/render_asset_loader.h"
#include "render/render_resource_service.h"
#include "scene/prefab_asset_loader.h"
#include "scene/prefab_runtime_instance.h"

#include <gneiss/world.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view prefab_json = R"({
  "format":"gneiss.prefab",
  "version":1,
  "prefab_uuid":"10000000-0000-4000-8000-000000000001",
  "objects":[
    {
      "uuid":"10000000-0000-4000-8000-000000000002",
      "parent":null,
      "transform":{"translation":[1,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
      "components":{}
    },
    {
      "uuid":"10000000-0000-4000-8000-000000000003",
      "parent":"10000000-0000-4000-8000-000000000002",
      "transform":{"translation":[0,2,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
      "components":{}
    }
  ]
})";

constexpr std::string_view broken_prefab_json = R"({
  "format":"gneiss.prefab",
  "version":1,
  "prefab_uuid":"10000000-0000-4000-8000-000000000011",
  "objects":[{
    "uuid":"10000000-0000-4000-8000-000000000012",
    "parent":null,
    "transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
    "components":{"mesh_renderer":{"mesh":"asset://missing.mesh","material":"asset://missing.material"}}
  }]
})";

class memory_file_system final : public gneiss::asset_internal::file_system {
public:
  [[nodiscard]] gneiss_result read(std::string_view path,
                                   std::vector<std::byte>& out_bytes) const noexcept override {
    const auto content =
        path == "lamp.prefab.json"
            ? prefab_json
            : (path == "broken.prefab.json" ? broken_prefab_json : std::string_view{});
    if (content.empty()) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    try {
      out_bytes.resize(content.size());
      for (std::size_t index = 0; index < content.size(); ++index) {
        out_bytes[index] = static_cast<std::byte>(content[index]);
      }
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }
};

} // namespace

int main() try {
  gneiss_world world = GNEISS_NULL_WORLD;
  const gneiss_world_desc world_desc = GNEISS_WORLD_DESC_INIT;
  if (gneiss_world_create(&world_desc, &world) != GNEISS_SUCCESS) {
    return 1;
  }

  gneiss::asset_internal::virtual_file_system file_system;
  gneiss::asset_internal::resource_cache cache;
  gneiss::render_internal::render_resource_service resources;
  gneiss::render_internal::render_asset_loader render_loader(file_system, cache, resources);
  gneiss::scene_internal::prefab_asset_loader prefab_loader(file_system, cache);
  if (file_system.mount("asset://prefabs/", std::make_shared<memory_file_system>()) !=
      GNEISS_SUCCESS) {
    return 2;
  }
  gneiss::scene_internal::scene_diagnostic diagnostic;
  gneiss::scene_internal::prefab_asset_lease first_lease;
  gneiss::scene_internal::prefab_asset_lease second_lease;
  if (prefab_loader.acquire("asset://prefabs/lamp.prefab.json", first_lease, diagnostic) !=
          GNEISS_SUCCESS ||
      prefab_loader.acquire("asset://prefabs/lamp.prefab.json", second_lease, diagnostic) !=
          GNEISS_SUCCESS) {
    return 3;
  }

  gneiss_transform first_transform = GNEISS_TRANSFORM_IDENTITY;
  first_transform.translation[0] = 10.0F;
  gneiss_transform second_transform = GNEISS_TRANSFORM_IDENTITY;
  second_transform.translation[0] = -5.0F;
  std::unique_ptr<gneiss::scene_internal::prefab_runtime_instance> first;
  std::unique_ptr<gneiss::scene_internal::prefab_runtime_instance> second;
  if (gneiss::scene_internal::prefab_runtime_instance::create(
          world, render_loader, std::move(first_lease), "20000000-0000-4000-8000-000000000001",
          GNEISS_NULL_SCENE_NODE_ID, first_transform, first) != GNEISS_SUCCESS ||
      gneiss::scene_internal::prefab_runtime_instance::create(
          world, render_loader, std::move(second_lease), "20000000-0000-4000-8000-000000000002",
          GNEISS_NULL_SCENE_NODE_ID, second_transform, second) != GNEISS_SUCCESS ||
      first->node_count() != 2U || second->node_count() != 2U) {
    return 4;
  }

  const auto first_source_root = first->find_node("10000000-0000-4000-8000-000000000002");
  const auto second_source_root = second->find_node("10000000-0000-4000-8000-000000000002");
  gneiss_transform first_world = GNEISS_TRANSFORM_IDENTITY;
  gneiss_transform second_world = GNEISS_TRANSFORM_IDENTITY;
  std::uint64_t entity_count = 0;
  if (first_source_root == GNEISS_NULL_SCENE_NODE_ID ||
      second_source_root == GNEISS_NULL_SCENE_NODE_ID || first_source_root == second_source_root ||
      gneiss_scene_node_get_world_transform(world, first_source_root, &first_world) !=
          GNEISS_SUCCESS ||
      gneiss_scene_node_get_world_transform(world, second_source_root, &second_world) !=
          GNEISS_SUCCESS ||
      first_world.translation[0] != 11.0F || second_world.translation[0] != -4.0F ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 6U) {
    return 5;
  }

  gneiss::scene_internal::prefab_asset_lease failure_lease;
  if (prefab_loader.acquire("asset://prefabs/lamp.prefab.json", failure_lease, diagnostic) !=
      GNEISS_SUCCESS) {
    return 6;
  }
  gneiss_transform invalid_transform = GNEISS_TRANSFORM_IDENTITY;
  invalid_transform.scale[1] = 0.0F;
  std::unique_ptr<gneiss::scene_internal::prefab_runtime_instance> failed;
  if (gneiss::scene_internal::prefab_runtime_instance::create(
          world, render_loader, std::move(failure_lease), "20000000-0000-4000-8000-000000000003",
          GNEISS_NULL_SCENE_NODE_ID, invalid_transform, failed) != GNEISS_ERROR_INVALID_ARGUMENT ||
      failed || gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS ||
      entity_count != 6U) {
    return 7;
  }

  gneiss::scene_internal::prefab_asset_lease broken_lease;
  if (prefab_loader.acquire("asset://prefabs/broken.prefab.json", broken_lease, diagnostic) !=
      GNEISS_SUCCESS) {
    return 8;
  }
  if (gneiss::scene_internal::prefab_runtime_instance::create(
          world, render_loader, std::move(broken_lease), "20000000-0000-4000-8000-000000000004",
          GNEISS_NULL_SCENE_NODE_ID, first_transform, failed) != GNEISS_ERROR_NOT_FOUND ||
      failed || gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS ||
      entity_count != 6U) {
    return 9;
  }

  first.reset();
  if (gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 3U ||
      gneiss_scene_node_get_world_transform(world, first_source_root, &first_world) !=
          GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_scene_node_get_world_transform(world, second_source_root, &second_world) !=
          GNEISS_SUCCESS) {
    return 10;
  }
  second.reset();
  if (gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 0U ||
      gneiss_world_destroy(world) != GNEISS_SUCCESS) {
    return 11;
  }
  return 0;
} catch (...) {
  return 99;
}
