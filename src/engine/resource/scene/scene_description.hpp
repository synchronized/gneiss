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

struct camera_description final
    : meta::object_type<camera_description, component_description_base> {
  camera_description() = default;
  camera_description(math::camera_settings value, bool primary = false)
      : settings(value), is_primary(primary) {}
  inline static constexpr std::string_view type_name = "camera_description";
  inline static constexpr engine::guid type_guid =
      engine::guid::literal("11a0217a-2eab-4f61-8011-4a63a26e0101");
  math::camera_settings settings{};
  bool is_primary = false;
};

struct mesh_renderer_description final
    : meta::object_type<mesh_renderer_description, component_description_base> {
  mesh_renderer_description() = default;
  mesh_renderer_description(std::string mesh, std::string material)
      : mesh_uri(std::move(mesh)), material_uri(std::move(material)) {}
  inline static constexpr std::string_view type_name = "mesh_renderer_description";
  inline static constexpr engine::guid type_guid =
      engine::guid::literal("11a0217a-2eab-4f61-8011-4a63a26e0102");
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
  static constexpr std::string_view type_name = "prefab_reference_description";
  static constexpr guid type_guid = guid::literal("97210000-0000-0000-0000-000000000000");
  std::string uri;
  std::vector<prefab_property_override> overrides;
  std::vector<prefab_component_override> component_overrides{};
};

struct object_description final {
  static constexpr std::string_view type_name = "object_description";
  static constexpr guid type_guid = guid::literal("97040000-0000-0000-0000-000000000000");
  gneiss::engine::guid uuid;
  std::string name;
  gneiss::engine::guid parent_uuid{};
  /** 相对父对象的局部变换；默认单位变换。 */
  math::transform transform{};
  component_descriptions components;
  std::optional<prefab_reference_description> prefab{};
};

struct scene_description final {
  inline static constexpr std::string_view type_name = "scene_description";
  inline static constexpr engine::guid type_guid{
      engine::guid::literal("4b91d830-28b7-4257-815f-673a9e6b7011")};
  inline static constexpr std::size_t maximum_source_bytes = 16U * 1024U * 1024U;
  gneiss::engine::guid uuid;
  std::vector<object_description> objects;
};

template <> struct type_identity<prefab_component_override> {
  static constexpr std::string_view name() noexcept { return "prefab_component_override"; }
  static constexpr engine::guid guid() noexcept {
    return engine::guid::literal("97220000-0000-0000-0000-000000000000");
  }
};

} // namespace gneiss::engine

#endif
