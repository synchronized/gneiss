// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/prefab_description.h"
#include "scene/scene_tree.h"

#include <gneiss/core/result.h>

#include <string>
#include <string_view>

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
      "components":{}
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

} // namespace

int main() {
  gneiss::scene_internal::prefab_description prefab;
  gneiss::scene_internal::scene_diagnostic diagnostic;
  if (gneiss::scene_internal::parse_prefab_description(valid_prefab, prefab, diagnostic) !=
          GNEISS_SUCCESS ||
      prefab.source_schema_version != 1U || prefab.objects.size() != 2U ||
      prefab.objects[1].parent_uuid != prefab.objects[0].uuid ||
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
  return 0;
}
