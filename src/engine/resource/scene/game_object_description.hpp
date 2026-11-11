// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/math/camera.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/meta/object_type.hpp"
#include "engine/resource/scene/component_description.hpp"
#include "engine/resource/scene/object_address.hpp"
#include "engine/resource/scene/prefab_property_data.hpp"
#include <optional>
#include <string>
#include <vector>

namespace gneiss {
struct camera_description final
    : meta::object_type<camera_description, component_description_base> {
  camera_description() = default;
  camera_description(math::camera_settings value, bool primary = false)
      : settings(value), is_primary(primary) {}
  inline static constexpr std::string_view type_name = "camera_description";
  inline static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("11a0217a-2eab-4f61-8011-4a63a26e0101");
  math::camera_settings settings{};
  bool is_primary = false;
};

struct mesh_component_description final
    : meta::object_type<mesh_component_description, component_description_base> {
  mesh_component_description() = default;
  mesh_component_description(std::string mesh, std::string material)
      : mesh_uri(std::move(mesh)), material_uri(std::move(material)) {}
  inline static constexpr std::string_view type_name = "mesh_component_description";
  inline static constexpr gneiss::guid type_guid =
      gneiss::guid::literal("11a0217a-2eab-4f61-8011-4a63a26e0102");
  std::string mesh_uri;
  std::string material_uri;
};

/** 无 added 表示移除来源组件；有值表示实例新增组件。 */
struct prefab_component_override final {
  guid source_node_uuid;
  guid type_guid;
  std::optional<component_description> added{};
  guid component_uuid{};
};
struct prefab_reference_description final {
  static constexpr std::string_view type_name = "prefab_reference_description";
  static constexpr guid type_guid = guid::literal("1631bf8f-97cd-437b-9f6f-72b9487977e0");
  std::string uri;
  std::vector<prefab_property_override> overrides;
  std::vector<prefab_component_override> component_overrides{};
};

struct game_object_description final {
  static constexpr std::string_view type_name = "game_object_description";
  static constexpr guid type_guid = guid::literal("d37553e3-45c8-4a29-ae26-ada3069d81ec");
  gneiss::guid uuid;
  std::string name;
  object_address parent{};
  /** 相对父对象的局部变换；默认单位变换。 */
  math::transform transform{};
  component_descriptions components;
  std::optional<prefab_reference_description> prefab{};
};

template <> struct type_identity<prefab_component_override> {
  static constexpr std::string_view name() noexcept { return "prefab_component_override"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("929b6330-9200-4f3b-b707-d1fef184eda3");
  }
};

} // namespace gneiss

namespace gneiss::meta {
template <> struct sequence_identity_traits<std::vector<game_object_description>> {
  static constexpr bool enabled = true;
  static guid identity(const game_object_description& object) noexcept { return object.uuid; }
};
}
