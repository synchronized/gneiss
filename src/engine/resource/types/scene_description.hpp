// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_DESCRIPTION_HPP_
#define GNEISS_SCENE_SCENE_DESCRIPTION_HPP_

#include "engine/core/result.hpp"

#include "engine/resource/types/prefab_property_data.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine {
class virtual_file_system;
}

namespace gneiss::engine {

struct scene_diagnostic final {
  gneiss::engine::result result = gneiss::engine::result::success;
  std::size_t byte_offset = 0;
  std::string path;
  std::string message;
};

struct camera_description final {
  float vertical_field_of_view_radians = 0.0F;
  float near_plane = 0.0F;
  float far_plane = 0.0F;
  bool is_primary = false;
};

struct mesh_renderer_description final {
  std::string mesh_uri;
  std::string material_uri;
};

struct object_description final {
  std::string uuid;
  std::string name;
  std::optional<std::string> parent_uuid;
  std::array<float, 3> translation{};
  std::array<float, 4> rotation{};
  std::array<float, 3> scale{};
  std::optional<camera_description> camera;
  std::optional<mesh_renderer_description> mesh_renderer;
};

struct prefab_instance_description final {
  std::string instance_uuid;
  std::string name;
  std::optional<std::string> parent_uuid;
  std::string prefab_uri;
  std::array<float, 3> translation{};
  std::array<float, 4> rotation{};
  std::array<float, 3> scale{};
  std::vector<prefab_property_override> overrides;
};

struct scene_description final {
  std::uint32_t source_schema_version = 0;
  std::string uuid;
  std::vector<object_description> objects;
  std::vector<prefab_instance_description> prefab_instances;
  std::string author_json;
};

[[nodiscard]] gneiss::engine::result
parse_scene_description(std::string_view json, scene_description& out_scene,
                        scene_diagnostic& out_diagnostic) noexcept;

[[nodiscard]] gneiss::engine::result
load_scene_description(const gneiss::engine::virtual_file_system& file_system, std::string_view uri,
                       scene_description& out_scene, scene_diagnostic& out_diagnostic) noexcept;

/** 输出当前 Schema 作者 JSON；保留受支持文档中的未知字段。 */
[[nodiscard]] gneiss::engine::result serialize_scene_description(const scene_description& scene,
                                                                 std::string& out_json) noexcept;

} // namespace gneiss::engine

#endif
