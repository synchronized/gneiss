// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "builtin_resource_registration.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/property_edit_policy.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/core/serialization/policy.hpp"
#include "engine/resource/asset_serialization.hpp"
#include "engine/resource/input/action_map.hpp"
#include "engine/resource/material.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/mesh.hpp"
#include "engine/resource/mesh/mesh_description.hpp"
#include "engine/resource/prefab.hpp"
#include "engine/resource/scene.hpp"
#include "engine/resource/scene/scene_validation.hpp"
#include "engine/resource/texture.hpp"
namespace gneiss::engine {
error register_component_descriptions() noexcept {
  // 基类身份字段使用保留编号，派生组件业务字段从 1 开始。
  auto status =
      meta::registration<component_description_base>{}
          .metadata(gneiss::engine::serialization::reference_policy{.polymorphic = true})
          .property<&component_description_base::uuid>(meta::property_id{0xffffffffU}, "uuid")(
              property_edit_policy{.read_only = true})(
              gneiss::engine::serialization::field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<prefab_property_target>{}
          .property<&prefab_property_target::source_node_uuid>(meta::property_id{1},
                                                               "source_node_uuid")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_property_target::target_type_guid>(meta::property_id{2},
                                                               "target_type_guid")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_property_target::component_uuid>(meta::property_id{3},
                                                             "component_uuid")(
              gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<prefab_property_override_key>{}
          .property<&prefab_property_override_key::target>(meta::property_id{1}, "target")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_property_override_key::path>(meta::property_id{2}, "path")(
              gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<prefab_property_override>{}
          .property<&prefab_property_override::key>(meta::property_id{1}, "key")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_property_override::operation>(meta::property_id{2}, "operation")(
              gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<prefab_component_override>{}
          .property<&prefab_component_override::source_node_uuid>(meta::property_id{1},
                                                                  "source_node_uuid")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_component_override::type_guid>(meta::property_id{2}, "type_guid")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_component_override::added>(meta::property_id{3}, "added")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_component_override::component_uuid>(meta::property_id{4},
                                                                "component_uuid")(
              gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<prefab_reference_description>{}
          .property<&prefab_reference_description::uri>(meta::property_id{1}, "uri")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_reference_description::overrides>(meta::property_id{2}, "overrides")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_reference_description::component_overrides>(meta::property_id{3},
                                                                        "component_overrides")
          .commit();
  if (status.failed())
    return status;
  status = meta::registration<object_address>{}
               .property<&object_address::object>(meta::property_id{1}, "object")(
                   gneiss::engine::serialization::field_policy{.required = true})
               .property<&object_address::source>(meta::property_id{2}, "source")(
                   gneiss::engine::serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration<game_object_description>{}
               .property<&game_object_description::uuid>(meta::property_id{1}, "uuid")(
                   gneiss::engine::serialization::field_policy{.required = true})
               .property<&game_object_description::name>(meta::property_id{2}, "name")
               .property<&game_object_description::parent>(meta::property_id{3}, "parent")(
                   gneiss::engine::serialization::field_policy{.required = true})
               .property<&game_object_description::transform>(meta::property_id{4}, "transform")(
                   gneiss::engine::serialization::field_policy{.required = true})
               .property<&game_object_description::components>(meta::property_id{5}, "components")
               .property<&game_object_description::prefab>(meta::property_id{6}, "prefab")
               .commit();
  if (status.failed())
    return status;
  status = meta::registration<camera_description>{}
               .base<component_description_base>()
               .property<&camera_description::settings>(meta::property_id{1}, "settings")(
                   gneiss::engine::serialization::field_policy{.required = true})
               .property<&camera_description::is_primary>(meta::property_id{4}, "is_primary")(
                   property_edit_policy{.read_only = true})(
                   gneiss::engine::serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  return meta::registration<mesh_renderer_description>{}
      .base<component_description_base>()
      .property<&mesh_renderer_description::mesh_uri>(meta::property_id{1}, "mesh")(
          gneiss::engine::serialization::field_policy{.required = true})
      .property<&mesh_renderer_description::material_uri>(meta::property_id{2}, "material")(
          gneiss::engine::serialization::field_policy{.required = true})
      .extend(gneiss::engine::serialization::validation<&validate_mesh_renderer_description>)
      .commit();
}
error register_builtin_resource_types() noexcept {
  auto status = register_component_descriptions();
  if (status.failed())
    return status;
  status = meta::registration<engine::texture>{}
               .guid(guid::literal("601bc0bf-b255-4eb1-a51f-f06b59e3ff3d"))
               .name("texture")
               .metadata(serialization::reference_policy{
                   .encode = detail::encode_asset_reference<engine::texture>,
                   .decode = detail::decode_asset_reference<engine::texture>,
                   .capture = detail::capture_asset_reference<engine::texture>})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration<engine::mesh>{}
               .guid(guid::literal("3c7a40a9-3b99-4955-810e-819d3b160590"))
               .name("mesh")
               .metadata(serialization::reference_policy{
                   .encode = detail::encode_asset_reference<engine::mesh>,
                   .decode = detail::decode_asset_reference<engine::mesh>,
                   .capture = detail::capture_asset_reference<engine::mesh>})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration<engine::material>{}
               .guid(guid::literal("c8edeac7-022d-446f-972f-941b5d193b66"))
               .name("material")
               .metadata(serialization::reference_policy{
                   .encode = detail::encode_asset_reference<engine::material>,
                   .decode = detail::decode_asset_reference<engine::material>,
                   .capture = detail::capture_asset_reference<engine::material>})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration<engine::scene>{}
               .guid(guid::literal("83dc2409-ec41-4eec-85a1-c7563b421a71"))
               .name("scene")
               .metadata(serialization::reference_policy{
                   .encode = detail::encode_asset_reference<engine::scene>,
                   .decode = detail::decode_asset_reference<engine::scene>,
                   .capture = detail::capture_asset_reference<engine::scene>})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration<engine::prefab>{}
               .guid(guid::literal("114e020a-d5e0-4941-8d6c-a611caa26448"))
               .name("prefab")
               .metadata(serialization::reference_policy{
                   .encode = detail::encode_asset_reference<engine::prefab>,
                   .decode = detail::decode_asset_reference<engine::prefab>,
                   .capture = detail::capture_asset_reference<engine::prefab>})
               .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<mesh_description>{}
          .property<&mesh_description::topology>(meta::property_id{1}, "topology")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&mesh_description::vertices>(meta::property_id{2}, "vertices")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&mesh_description::uvs>(meta::property_id{3}, "uvs")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&mesh_description::normals>(meta::property_id{4}, "normals")(
              gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status = meta::enum_builder<material_alpha_mode>{}
               .value("opaque", material_alpha_mode::opaque)
               .value("mask", material_alpha_mode::mask)
               .value("blend", material_alpha_mode::blend)
               .commit();
  if (status.failed())
    return status;
  status = meta::enum_builder<texture_filter>{}
               .value("nearest", texture_filter::nearest)
               .value("linear", texture_filter::linear)
               .commit();
  if (status.failed())
    return status;
  status = meta::enum_builder<texture_mip_filter>{}
               .value("none", texture_mip_filter::none)
               .value("nearest", texture_mip_filter::nearest)
               .value("linear", texture_mip_filter::linear)
               .commit();
  if (status.failed())
    return status;
  status = meta::enum_builder<texture_address>{}
               .value("repeat", texture_address::repeat)
               .value("clamp", texture_address::clamp)
               .value("mirror", texture_address::mirror)
               .commit();
  if (status.failed())
    return status;
  status = meta::enum_builder<texture_color_space>{}
               .value("linear", texture_color_space::linear)
               .value("srgb", texture_color_space::srgb)
               .commit();
  if (status.failed())
    return status;
  status = meta::enum_builder<texture_usage>{}
               .value("default", texture_usage::default_usage)
               .value("normal", texture_usage::normal)
               .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<texture_description>{}
          .property<&texture_description::uri>(meta::property_id{1}, "source")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&texture_description::color_space>(meta::property_id{2}, "color_space")(
              gneiss::engine::serialization::field_policy{.required = true, .enum_name = true})
          .property<&texture_description::usage>(meta::property_id{3}, "usage")(
              gneiss::engine::serialization::field_policy{.required = true, .enum_name = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<texture_sampling>{}
          .property<&texture_sampling::uv_set>(meta::property_id{1}, "uv_set")
          .property<&texture_sampling::mag_filter>(meta::property_id{2}, "mag_filter")
          .property<&texture_sampling::min_filter>(meta::property_id{3}, "min_filter")
          .property<&texture_sampling::mip_filter>(meta::property_id{4}, "mip_filter")
          .property<&texture_sampling::address_u>(meta::property_id{5}, "address_u")
          .property<&texture_sampling::address_v>(meta::property_id{6}, "address_v")
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<material_parameters>{}
          .property<&material_parameters::color>(meta::property_id{1}, "color")
          .property<&material_parameters::metallic>(meta::property_id{2}, "metallic")
          .property<&material_parameters::roughness>(meta::property_id{3}, "roughness")
          .property<&material_parameters::normal_scale>(meta::property_id{4}, "normal_scale")
          .property<&material_parameters::occlusion_strength>(meta::property_id{5},
                                                              "occlusion_strength")
          .property<&material_parameters::emissive>(meta::property_id{6}, "emissive")
          .property<&material_parameters::alpha_mode>(meta::property_id{7}, "alpha_mode")
          .property<&material_parameters::double_sided>(meta::property_id{8}, "double_sided")
          .property<&material_parameters::alpha_cutoff>(meta::property_id{9}, "alpha_cutoff")
          .property<&material_parameters::sampling>(meta::property_id{10}, "sampling")
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<material_description>{}
          .schema_version(6)
          .property<&material_description::parameters>(meta::property_id{1}, "parameters")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&material_description::texture_uris>(meta::property_id{2}, "texture_uris")(
              gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;

  status =
      meta::registration<action_binding>{}
          .property<&action_binding::physical_key>(meta::property_id{1}, "key")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&action_binding::scale>(meta::property_id{2}, "scale")(
              gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<action_definition>{}
          .property<&action_definition::name>(meta::property_id{1}, "name")(
              gneiss::engine::serialization::field_policy{.required = true})
          .property<&action_definition::bindings>(meta::property_id{2}, "bindings")(
              gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<action_map_description>{}
          .schema_version(1)
          .property<&action_map_description::actions>(meta::property_id{1}, "actions")(
              gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status = meta::registration<scene_description>{}
               .property<&scene_description::uuid>(meta::property_id{1}, "scene_uuid")(
                   gneiss::engine::serialization::field_policy{.required = true})
               .property<&scene_description::objects>(meta::property_id{2}, "objects")(
                   gneiss::engine::serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  return meta::registration<prefab_description>{}
      .property<&prefab_description::uuid>(meta::property_id{1}, "prefab_uuid")(
          gneiss::engine::serialization::field_policy{.required = true})
      .property<&prefab_description::root_uuid>(meta::property_id{2}, "root_uuid")(
          gneiss::engine::serialization::field_policy{.required = true})
      .property<&prefab_description::objects>(meta::property_id{3}, "objects")(
          gneiss::engine::serialization::field_policy{.required = true})
      .commit();
}
}
