// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_DESCRIPTION_HPP_
#define GNEISS_SCENE_SCENE_DESCRIPTION_HPP_

#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"

#include "engine/resource/scene/component_description.hpp"
#include "engine/resource/scene/prefab_property_data.hpp"

#include "engine/core/math/camera.hpp"
#include "engine/core/math/transform.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine {

struct scene_diagnostic final {
  engine::error error = engine::error::success;
  std::size_t byte_offset = 0;
  std::string path{};
  std::string message{};
};

struct camera_description final {
  inline static constexpr std::string_view type_name = "camera_description";
  inline static constexpr engine::guid type_guid =
      engine::guid::from_bytes({0x11, 0xa0, 0x21, 0x7a, 0x2e, 0xab, 0x4f, 0x61, 0x80, 0x11, 0x4a,
                                0x63, 0xa2, 0x6e, 0x01, 0x01});
  math::camera_settings settings{};
  bool is_primary = false;
};

struct mesh_renderer_description final {
  inline static constexpr std::string_view type_name = "mesh_renderer_description";
  inline static constexpr engine::guid type_guid =
      engine::guid::from_bytes({0x11, 0xa0, 0x21, 0x7a, 0x2e, 0xab, 0x4f, 0x61, 0x80, 0x11, 0x4a,
                                0x63, 0xa2, 0x6e, 0x01, 0x02});
  std::string mesh_uri;
  std::string material_uri;
};

/** 无 added 表示移除来源组件；有值表示实例新增组件。 */
struct prefab_component_override final {
  guid source_node_uuid;
  guid type_guid;
  std::optional<component_description> added{};
};
struct prefab_reference_description final {
  std::string uri;
  std::vector<prefab_property_override> overrides;
  std::vector<prefab_component_override> component_overrides{};
};

struct object_description final {
  static constexpr std::string_view type_name = "object_description";
  static constexpr guid type_guid = guid::from_bytes({0x97, 4});
  gneiss::engine::guid uuid;
  std::string name;
  std::optional<gneiss::engine::guid> parent_uuid;
  /** 相对父对象的局部变换；默认单位变换。 */
  math::transform transform{};
  component_descriptions components;
  std::optional<prefab_reference_description> prefab{};
};

struct scene_description final {
  inline static constexpr std::string_view type_name = "scene_description";
  inline static constexpr engine::guid type_guid{
      engine::guid::from_bytes({0x4b, 0x91, 0xd8, 0x30, 0x28, 0xb7, 0x42, 0x57, 0x81, 0x5f, 0x67,
                                0x3a, 0x9e, 0x6b, 0x70, 0x11})};
  inline static constexpr std::size_t maximum_source_bytes = 16U * 1024U * 1024U;
  std::uint32_t source_schema_version = 0;
  gneiss::engine::guid uuid;
  std::vector<object_description> objects;
};

} // namespace gneiss::engine

#endif
