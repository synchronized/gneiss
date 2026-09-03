// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/file_system.h"
#include "asset/virtual_file_system.h"
#include "scene/scene_description.h"

#include <gneiss/core/result.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view valid_scene = R"({
  "format":"gneiss.scene","version":4,
  "scene_uuid":"00000000-0000-4000-8000-000000000001",
  "objects":[
    {"uuid":"00000000-0000-4000-8000-000000000002","parent":null,
     "transform":{"translation":[0,1,2],"rotation":[0,0,0,1],"scale":[1,1,1]},
     "components":{"camera":{"vertical_field_of_view_radians":1.0,"near_plane":0.1,"far_plane":100,"is_primary":true}}},
    {"uuid":"00000000-0000-4000-8000-000000000003","parent":"00000000-0000-4000-8000-000000000002",
     "transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
     "components":{"mesh_renderer":{"mesh":"asset://models/triangle.mesh","material":"asset://materials/default.material"}}}
  ],"prefab_instances":[],"unknown":true
})";

constexpr std::string_view prefab_scene = R"({
  "format":"gneiss.scene","version":4,
  "scene_uuid":"00000000-0000-4000-8000-000000000001",
  "objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,
    "transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{}}],
  "prefab_instances":[{
    "instance_uuid":"00000000-0000-4000-8000-000000000010","name":"Lamp",
    "parent":"00000000-0000-4000-8000-000000000002",
    "prefab":"asset://prefabs/lamp.prefab.json",
    "transform":{"translation":[1,2,3],"rotation":[0,0,0,1],"scale":[2,2,2]},
    "overrides":[{"source_node_uuid":"10000000-0000-4000-8000-000000000002",
      "type_id":"69644f20b2d24e488c7491f4f952ec2d","field_id":1,
      "value":{"kind":"vec3","value":[4,5,6]}}],"unknown":true
  }]
})";

[[nodiscard]] std::string replace_once(std::string text, std::string_view source,
                                       std::string_view replacement) {
  const auto position = text.find(source);
  if (position != std::string::npos) {
    text.replace(position, source.size(), replacement);
  }
  return text;
}

[[nodiscard]] bool fails_with(std::string_view json, gneiss_result expected,
                              std::string_view path) {
  gneiss::scene_internal::scene_description scene;
  gneiss::scene_internal::scene_diagnostic diagnostic;
  return gneiss::scene_internal::parse_scene_description(json, scene, diagnostic) == expected &&
         diagnostic.result == expected && diagnostic.path == path && scene.objects.empty();
}

class memory_file_system final : public gneiss::asset_internal::file_system {
public:
  explicit memory_file_system(std::string content) : content_(std::move(content)) {}

  [[nodiscard]] gneiss_result read(std::string_view path,
                                   std::vector<std::byte>& out_bytes) const noexcept override {
    if (path != "main.scene.json") {
      return GNEISS_ERROR_NOT_FOUND;
    }
    try {
      out_bytes.resize(content_.size());
      for (std::size_t index = 0U; index < content_.size(); ++index) {
        out_bytes[index] = static_cast<std::byte>(content_[index]);
      }
      return GNEISS_SUCCESS;
    } catch (...) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
  }

private:
  std::string content_;
};

} // namespace

int main() try {
  gneiss::scene_internal::scene_description scene;
  gneiss::scene_internal::scene_diagnostic diagnostic;
  std::string serialized;
  if (gneiss::scene_internal::parse_scene_description(valid_scene, scene, diagnostic) !=
          GNEISS_SUCCESS ||
      scene.source_schema_version != 4U || scene.objects.size() != 2U || !scene.objects[0].camera ||
      !scene.objects[1].mesh_renderer ||
      gneiss::scene_internal::serialize_scene_description(scene, serialized) != GNEISS_SUCCESS ||
      serialized.find(R"("unknown":true)") == std::string::npos) {
    return 1;
  }

  const auto future = replace_once(std::string(valid_scene), "\"version\":4", "\"version\":5");
  const auto old = replace_once(std::string(valid_scene), "\"version\":4", "\"version\":3");
  const auto duplicate =
      replace_once(std::string(valid_scene), "00000000-0000-4000-8000-000000000003",
                   "00000000-0000-4000-8000-000000000002");
  const auto missing_parent =
      replace_once(std::string(valid_scene), R"("parent":"00000000-0000-4000-8000-000000000002")",
                   R"("parent":"00000000-0000-4000-8000-000000000099")");
  const auto cycle = replace_once(std::string(valid_scene), R"("parent":null)",
                                  R"("parent":"00000000-0000-4000-8000-000000000003")");
  const auto invalid_uri = replace_once(std::string(valid_scene), "asset://models/triangle.mesh",
                                        "asset://models/../triangle.mesh");
  const auto invalid_scale =
      replace_once(std::string(valid_scene), R"("scale":[1,1,1])", R"("scale":[1,0,1])");
  const auto invalid_rotation =
      replace_once(std::string(valid_scene), R"("rotation":[0,0,0,1])", R"("rotation":[0,0,0,2])");
  if (!fails_with(future, GNEISS_ERROR_UNSUPPORTED, "/version") ||
      !fails_with(old, GNEISS_ERROR_INVALID_ARGUMENT, "/version") ||
      !fails_with(duplicate, GNEISS_ERROR_INVALID_ARGUMENT, "/objects/1/uuid") ||
      !fails_with(missing_parent, GNEISS_ERROR_INVALID_ARGUMENT, "/objects/1/parent") ||
      !fails_with(cycle, GNEISS_ERROR_INVALID_ARGUMENT, "/objects/0/parent") ||
      !fails_with(invalid_uri, GNEISS_ERROR_INVALID_ARGUMENT,
                  "/objects/1/components/mesh_renderer") ||
      !fails_with(invalid_scale, GNEISS_ERROR_INVALID_ARGUMENT, "/objects/0/transform") ||
      !fails_with(invalid_rotation, GNEISS_ERROR_INVALID_ARGUMENT, "/objects/0/transform")) {
    return 2;
  }

  if (gneiss::scene_internal::parse_scene_description(prefab_scene, scene, diagnostic) !=
          GNEISS_SUCCESS ||
      scene.prefab_instances.size() != 1U || scene.prefab_instances[0].overrides.size() != 1U ||
      scene.prefab_instances[0].overrides[0].key.node.instance_uuid !=
          scene.prefab_instances[0].instance_uuid ||
      std::get<std::array<float, 3>>(scene.prefab_instances[0].overrides[0].value.payload)[0] !=
          4.0F ||
      gneiss::scene_internal::serialize_scene_description(scene, serialized) != GNEISS_SUCCESS ||
      serialized.find(R"("kind":"vec3")") == std::string::npos ||
      serialized.find(R"("unknown":true)") == std::string::npos) {
    return 3;
  }

  const auto duplicate_override = replace_once(
      std::string(prefab_scene), R"("overrides":[{)",
      R"("overrides":[{"source_node_uuid":"10000000-0000-4000-8000-000000000002","type_id":"69644f20b2d24e488c7491f4f952ec2d","field_id":1,"value":{"kind":"vec3","value":[7,8,9]}},{)");
  const auto bad_type = replace_once(std::string(prefab_scene), "69644f20b2d24e488c7491f4f952ec2d",
                                     "69644F20b2d24e488c7491f4f952ec2d");
  const auto bad_value =
      replace_once(std::string(prefab_scene), R"("value":[4,5,6])", R"("value":[4,5])");
  if (!fails_with(duplicate_override, GNEISS_ERROR_INVALID_ARGUMENT,
                  "/prefab_instances/0/overrides") ||
      !fails_with(bad_type, GNEISS_ERROR_INVALID_ARGUMENT,
                  "/prefab_instances/0/overrides/0/type_id") ||
      !fails_with(bad_value, GNEISS_ERROR_INVALID_ARGUMENT,
                  "/prefab_instances/0/overrides/0/value")) {
    return 4;
  }

  constexpr std::array<std::string_view, 8> other_values{
      R"("kind":"bool","value":true)",
      R"("kind":"int64","value":-5)",
      R"("kind":"uint64","value":5)",
      R"("kind":"float32","value":1.25)",
      R"("kind":"float64","value":2.5)",
      R"("kind":"string","value":"text")",
      R"("kind":"type_id","value":"69644f20b2d24e488c7491f4f952ec2d")",
      R"("kind":"quaternion","value":[0,0,0,1])",
  };
  for (const auto value : other_values) {
    const auto candidate =
        replace_once(std::string(prefab_scene), R"("kind":"vec3","value":[4,5,6])", value);
    if (gneiss::scene_internal::parse_scene_description(candidate, scene, diagnostic) !=
            GNEISS_SUCCESS ||
        gneiss::scene_internal::serialize_scene_description(scene, serialized) != GNEISS_SUCCESS) {
      return 5;
    }
  }

  gneiss::asset_internal::virtual_file_system file_system;
  if (file_system.mount("asset://scenes/", std::make_shared<memory_file_system>(
                                               std::string(valid_scene))) != GNEISS_SUCCESS ||
      gneiss::scene_internal::load_scene_description(file_system, "asset://scenes/main.scene.json",
                                                     scene, diagnostic) != GNEISS_SUCCESS) {
    return 6;
  }
  return fails_with("{\"format\":", GNEISS_ERROR_INVALID_ARGUMENT, "") ? 0 : 7;
} catch (...) {
  return 99;
}
