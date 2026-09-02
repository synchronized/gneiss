// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/file_system.h"
#include "asset/resource_cache.h"
#include "asset/virtual_file_system.h"
#include "scene/prefab_asset_loader.h"
#include "scene/prefab_description.h"
#include "scene/scene_tree.h"

#include <gneiss/core/result.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view valid_prefab = R"({
  "format":"gneiss.prefab",
  "version":1,
  "prefab_uuid":"10000000-0000-4000-8000-000000000001",
  "objects":[
    {
      "uuid":"10000000-0000-4000-8000-000000000002",
      "name":"Prefab Root",
      "parent":null,
      "transform":{"translation":[1,2,3],"rotation":[0,0,0,1],"scale":[1,1,1]},
      "components":{"mesh_renderer":{"mesh":"asset://models/lantern.mesh","material":"asset://materials/lantern.material"}}
    },
    {
      "uuid":"10000000-0000-4000-8000-000000000003",
      "name":"Child",
      "parent":"10000000-0000-4000-8000-000000000002",
      "transform":{"translation":[0,1,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
      "components":{}
    }
  ]
})";

[[nodiscard]] std::string replace_once(std::string text, std::string_view source,
                                       std::string_view replacement) {
  const auto position = text.find(source);
  if (position != std::string::npos) {
    text.replace(position, source.size(), replacement);
  }
  return text;
}

class memory_file_system final : public gneiss::asset_internal::file_system {
public:
  explicit memory_file_system(std::string content) : content_(std::move(content)) {}

  [[nodiscard]] gneiss_result read(std::string_view path,
                                   std::vector<std::byte>& out_bytes) const noexcept override {
    if (path != "lamp.prefab.json") {
      return GNEISS_ERROR_NOT_FOUND;
    }
    try {
      ++read_count;
      out_bytes.resize(content_.size());
      for (std::size_t index = 0; index < content_.size(); ++index) {
        out_bytes[index] = static_cast<std::byte>(content_[index]);
      }
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }

  mutable std::size_t read_count = 0;

private:
  std::string content_;
};

} // namespace

int main() {
  gneiss::scene_internal::prefab_description prefab;
  gneiss::scene_internal::scene_diagnostic diagnostic;
  if (gneiss::scene_internal::parse_prefab_description(valid_prefab, prefab, diagnostic) !=
          GNEISS_SUCCESS ||
      prefab.source_schema_version != 1U || prefab.objects.size() != 2U ||
      prefab.objects[1].parent_uuid != prefab.objects[0].uuid || prefab.dependencies.size() != 2U ||
      prefab.dependencies[0] != "asset://materials/lantern.material" ||
      prefab.dependencies[1] != "asset://models/lantern.mesh" ||
      prefab.author_json != valid_prefab) {
    return 1;
  }

  const gneiss::scene_internal::prefab_author_address address{
      "20000000-0000-4000-8000-000000000001", prefab.objects[1].uuid};
  const gneiss::scene_internal::prefab_author_address different_instance{
      "20000000-0000-4000-8000-000000000002", prefab.objects[1].uuid};
  if (!gneiss::scene_internal::is_valid_prefab_author_address(address) ||
      address == different_instance) {
    return 2;
  }

  const auto multiple_roots =
      replace_once(std::string(valid_prefab), R"("parent":"10000000-0000-4000-8000-000000000002")",
                   R"("parent":null)");
  if (gneiss::scene_internal::parse_prefab_description(multiple_roots, prefab, diagnostic) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      diagnostic.path != "/objects" || !prefab.objects.empty()) {
    return 3;
  }

  const auto future = replace_once(std::string(valid_prefab), "\"version\":1", "\"version\":2");
  if (gneiss::scene_internal::parse_prefab_description(future, prefab, diagnostic) !=
          GNEISS_ERROR_UNSUPPORTED ||
      diagnostic.path != "/version") {
    return 4;
  }

  // 实例根作为源根的父节点，沿用 Scene Tree 的父子变换组合约定。
  gneiss::scene_internal::scene_tree tree(1U);
  gneiss_scene_node_id instance_root = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_node_id source_root = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_transform instance_transform = GNEISS_TRANSFORM_IDENTITY;
  instance_transform.translation[0] = 10.0F;
  instance_transform.scale[0] = 2.0F;
  gneiss_transform source_transform = GNEISS_TRANSFORM_IDENTITY;
  source_transform.translation[0] = 3.0F;
  if (tree.create(GNEISS_NULL_SCENE_NODE_ID, GNEISS_NULL_ENTITY_ID, &instance_root) !=
          GNEISS_SUCCESS ||
      tree.create(instance_root, GNEISS_NULL_ENTITY_ID, &source_root) != GNEISS_SUCCESS ||
      tree.set_local(instance_root, instance_transform) != GNEISS_SUCCESS ||
      tree.set_local(source_root, source_transform) != GNEISS_SUCCESS) {
    return 5;
  }
  gneiss_transform world = GNEISS_TRANSFORM_IDENTITY;
  if (tree.get_world(source_root, &world) != GNEISS_SUCCESS || world.translation[0] != 16.0F ||
      world.scale[0] != 2.0F) {
    return 6;
  }

  auto mounted = std::make_shared<memory_file_system>(std::string(valid_prefab));
  gneiss::asset_internal::virtual_file_system file_system;
  gneiss::asset_internal::resource_cache cache;
  gneiss::scene_internal::prefab_asset_loader loader(file_system, cache);
  gneiss::scene_internal::prefab_asset_lease first;
  gneiss::scene_internal::prefab_asset_lease second;
  if (file_system.mount("asset://prefabs/", mounted) != GNEISS_SUCCESS ||
      loader.acquire("asset://prefabs/lamp.prefab.json", first, diagnostic) != GNEISS_SUCCESS ||
      loader.acquire("asset://prefabs/lamp.prefab.json", second, diagnostic) != GNEISS_SUCCESS ||
      first.get() == nullptr || first.get() != second.get() || mounted->read_count != 1U ||
      cache.size() != 1U) {
    return 7;
  }
  gneiss::scene_internal::prefab_asset_lease invalid;
  if (loader.acquire("asset://prefabs/../lamp.prefab.json", invalid, diagnostic) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      invalid || diagnostic.message.empty()) {
    return 8;
  }
  return 0;
}
