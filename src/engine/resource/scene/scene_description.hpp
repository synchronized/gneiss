// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_DESCRIPTION_HPP_
#define GNEISS_SCENE_SCENE_DESCRIPTION_HPP_

#include "engine/core/guid.hpp"
#include "engine/core/result.hpp"

#include "engine/resource/scene/prefab_property_data.hpp"

#include "engine/core/math/types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
  gneiss::engine::math::vector3 translation{};
  gneiss::engine::math::quaternion rotation{0.0F, 0.0F, 0.0F, 0.0F};
  gneiss::engine::math::vector3 scale{};
  std::optional<camera_description> camera;
  std::optional<mesh_renderer_description> mesh_renderer;
};

struct prefab_instance_description final {
  std::string instance_uuid;
  std::string name;
  std::optional<std::string> parent_uuid;
  std::string prefab_uri;
  gneiss::engine::math::vector3 translation{};
  gneiss::engine::math::quaternion rotation{0.0F, 0.0F, 0.0F, 0.0F};
  gneiss::engine::math::vector3 scale{};
  std::vector<prefab_property_override> overrides;
};

struct scene_description final {
  inline static constexpr std::string_view type_name = "scene_description";
  inline static constexpr engine::guid type_guid{
      engine::guid::from_bytes({0x4b, 0x91, 0xd8, 0x30, 0x28, 0xb7, 0x42, 0x57, 0x81, 0x5f, 0x67,
                                0x3a, 0x9e, 0x6b, 0x70, 0x11})};
  inline static constexpr std::size_t maximum_source_bytes = 16U * 1024U * 1024U;
  std::uint32_t source_schema_version = 0;
  std::string uuid;
  std::vector<object_description> objects;
  std::vector<prefab_instance_description> prefab_instances;
  std::string author_json;
};

} // namespace gneiss::engine

#endif
