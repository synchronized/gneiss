// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/file_system.h"
#include "asset/virtual_file_system.h"
#include "scene/scene_description.h"

#include <gneiss/core/result.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view valid_scene = R"({
  "format":"gneiss.scene",
  "version":1,
  "scene_uuid":"00000000-0000-4000-8000-000000000001",
  "objects":[
    {
      "uuid":"00000000-0000-4000-8000-000000000002",
      "parent":null,
      "transform":{"translation":[0,1,2],"rotation":[0,0,0,1],"scale":[1,1,1]},
      "components":{"camera":{"vertical_field_of_view_radians":1.0,"near_plane":0.1,"far_plane":100,"primary":true}}
    },
    {
      "uuid":"00000000-0000-4000-8000-000000000003",
      "parent":"00000000-0000-4000-8000-000000000002",
      "transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
      "components":{"mesh_renderer":{"mesh":"asset://models/triangle.mesh","material":"asset://materials/default.material"}}
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
    if (path != "main.scene.json") {
      return GNEISS_ERROR_NOT_FOUND;
    }
    try {
      out_bytes.resize(content_.size());
      for (std::size_t index = 0; index < content_.size(); ++index) {
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

[[nodiscard]] bool fails_with(std::string_view json, gneiss_result expected,
                              std::string_view path) {
  gneiss::scene_internal::scene_description scene;
  gneiss::scene_internal::scene_diagnostic diagnostic;
  return gneiss::scene_internal::parse_scene_description(json, scene, diagnostic) == expected &&
         diagnostic.result == expected && diagnostic.path == path && scene.objects.empty();
}

} // namespace

int main() try {
  gneiss::scene_internal::scene_description scene;
  gneiss::scene_internal::scene_diagnostic diagnostic;
  if (gneiss::scene_internal::parse_scene_description(valid_scene, scene, diagnostic) !=
          GNEISS_SUCCESS ||
      scene.objects.size() != 2U || !scene.objects[0].camera || !scene.objects[1].mesh_renderer ||
      scene.objects[1].parent_uuid != scene.objects[0].uuid) {
    return 1;
  }

  const auto future = replace_once(std::string(valid_scene), "\"version\":1", "\"version\":2");
  const auto duplicate =
      replace_once(std::string(valid_scene), "00000000-0000-4000-8000-000000000003",
                   "00000000-0000-4000-8000-000000000002");
  const auto missing_parent =
      replace_once(std::string(valid_scene), R"("parent":"00000000-0000-4000-8000-000000000002")",
                   R"("parent":"00000000-0000-4000-8000-000000000099")");
  auto cycle = replace_once(std::string(valid_scene), "\"parent\":null",
                            R"("parent":"00000000-0000-4000-8000-000000000003")");
  const auto invalid_uri = replace_once(std::string(valid_scene), "asset://models/triangle.mesh",
                                        "asset://models/../triangle.mesh");
  const auto unknown =
      replace_once(std::string(valid_scene), "\"objects\":[", R"("unknown":true,"objects":[)");
  const auto invalid_number =
      replace_once(std::string(valid_scene), "\"scale\":[1,1,1]", "\"scale\":[1,0,1]");
  const auto invalid_rotation =
      replace_once(std::string(valid_scene), "\"rotation\":[0,0,0,1]", "\"rotation\":[0,0,0,2]");
  const auto multiple_primary =
      replace_once(std::string(valid_scene),
                   "\"mesh_renderer\":{\"mesh\":\"asset://models/"
                   "triangle.mesh\",\"material\":\"asset://materials/default.material\"}",
                   "\"camera\":{\"vertical_field_of_view_radians\":1.0,\"near_plane\":0.1,\"far_"
                   "plane\":100,\"primary\":true}");
  if (!fails_with(future, GNEISS_ERROR_UNSUPPORTED, "/version") ||
      !fails_with(duplicate, GNEISS_ERROR_INVALID_ARGUMENT, "/objects/1/uuid") ||
      !fails_with(missing_parent, GNEISS_ERROR_INVALID_ARGUMENT, "/objects/1/parent") ||
      !fails_with(cycle, GNEISS_ERROR_INVALID_ARGUMENT, "/objects/0/parent") ||
      !fails_with(invalid_uri, GNEISS_ERROR_INVALID_ARGUMENT,
                  "/objects/1/components/mesh_renderer") ||
      !fails_with(unknown, GNEISS_ERROR_INVALID_ARGUMENT, "/unknown") ||
      !fails_with(invalid_number, GNEISS_ERROR_INVALID_ARGUMENT, "/objects/0/transform") ||
      !fails_with(invalid_rotation, GNEISS_ERROR_INVALID_ARGUMENT, "/objects/0/transform") ||
      !fails_with(multiple_primary, GNEISS_ERROR_INVALID_ARGUMENT, "/objects")) {
    return 2;
  }

  gneiss::asset_internal::virtual_file_system file_system;
  if (file_system.mount("asset://scenes/", std::make_shared<memory_file_system>(
                                               std::string(valid_scene))) != GNEISS_SUCCESS ||
      gneiss::scene_internal::load_scene_description(file_system, "asset://scenes/main.scene.json",
                                                     scene, diagnostic) != GNEISS_SUCCESS ||
      scene.objects.size() != 2U) {
    return 3;
  }
  if (!fails_with("{\"format\":", GNEISS_ERROR_INVALID_ARGUMENT, "")) {
    return 4;
  }
  return 0;
} catch (...) {
  return 99;
}
