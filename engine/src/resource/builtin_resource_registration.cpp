// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/resource/builtin_resource_registration.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/property_edit_policy.hpp>
#include <gneiss/engine/core/serialization/json.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/engine/resource/asset_serialization.hpp>
#include <gneiss/engine/resource/input/action_map.hpp>
#include <gneiss/engine/resource/material.hpp>
#include <gneiss/engine/resource/material/material_description.hpp>
#include <gneiss/engine/resource/mesh.hpp>
#include <gneiss/engine/resource/mesh/mesh_description.hpp>
#include <gneiss/engine/resource/prefab.hpp>
#include <gneiss/engine/resource/scene.hpp>
#include <gneiss/engine/resource/scene/scene_validation.hpp>
#include <gneiss/engine/resource/texture.hpp>
namespace gneiss {
error register_component_descriptions() {
  // 基类身份字段使用保留编号，派生组件业务字段从 1 开始。
  auto status =
      meta::registration::type<component_description_base>()
          .metadata(gneiss::serialization::reference_policy{.polymorphic = true})
          .property<&component_description_base::uuid>(meta::property_id{0xffffffffU}, "uuid")(
              property_edit_policy{.read_only = true})(
              gneiss::serialization::field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<prefab_property_target>()
          .property<&prefab_property_target::source_node_uuid>(meta::property_id{1},
                                                               "source_node_uuid")(
              gneiss::serialization::field_policy{.required = true})
          .property<&prefab_property_target::target_type_guid>(meta::property_id{2},
                                                               "target_type_guid")(
              gneiss::serialization::field_policy{.required = true})
          .property<&prefab_property_target::component_uuid>(meta::property_id{3},
                                                             "component_uuid")(
              gneiss::serialization::field_policy{.required = true})
          .metadata(gneiss::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<prefab_property_override_key>()
          .property<&prefab_property_override_key::target>(meta::property_id{1}, "target")(
              gneiss::serialization::field_policy{.required = true})
          .property<&prefab_property_override_key::path>(meta::property_id{2}, "path")(
              gneiss::serialization::field_policy{.required = true})
          .metadata(gneiss::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<prefab_property_override>()
          .property<&prefab_property_override::key>(meta::property_id{1}, "key")(
              gneiss::serialization::field_policy{.required = true})
          .property<&prefab_property_override::operation>(meta::property_id{2}, "operation")(
              gneiss::serialization::field_policy{.required = true})
          .metadata(gneiss::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<prefab_component_override>()
          .property<&prefab_component_override::source_node_uuid>(meta::property_id{1},
                                                                  "source_node_uuid")(
              gneiss::serialization::field_policy{.required = true})
          .property<&prefab_component_override::type_guid>(meta::property_id{2}, "type_guid")(
              gneiss::serialization::field_policy{.required = true})
          .property<&prefab_component_override::added>(meta::property_id{3}, "added")(
              gneiss::serialization::field_policy{.required = true})
          .property<&prefab_component_override::component_uuid>(meta::property_id{4},
                                                                "component_uuid")(
              gneiss::serialization::field_policy{.required = true})
          .metadata(gneiss::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<prefab_reference_description>()
          .property<&prefab_reference_description::uri>(meta::property_id{1}, "uri")(
              gneiss::serialization::field_policy{.required = true})
          .property<&prefab_reference_description::overrides>(meta::property_id{2}, "overrides")(
              gneiss::serialization::field_policy{.required = true})
          .property<&prefab_reference_description::component_overrides>(meta::property_id{3},
                                                                        "component_overrides")
          .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<object_address>()
               .property<&object_address::object>(meta::property_id{1}, "object")(
                   gneiss::serialization::field_policy{.required = true})
               .property<&object_address::source>(meta::property_id{2}, "source")(
                   gneiss::serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<game_object_description>()
               .property<&game_object_description::uuid>(meta::property_id{1}, "uuid")(
                   gneiss::serialization::field_policy{.required = true})
               .property<&game_object_description::name>(meta::property_id{2}, "name")
               .property<&game_object_description::parent>(meta::property_id{3}, "parent")(
                   gneiss::serialization::field_policy{.required = true})
               .property<&game_object_description::transform>(meta::property_id{4}, "transform")(
                   gneiss::serialization::field_policy{.required = true})
               .property<&game_object_description::components>(meta::property_id{5}, "components")
               .property<&game_object_description::prefab>(meta::property_id{6}, "prefab")
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<camera_description>()
               .metadata(component_validation<camera_description, &validate_camera_description>)
               .base<component_description_base>()
               .property<&camera_description::settings>(meta::property_id{1}, "settings")(
                   gneiss::serialization::field_policy{.required = true})
               .property<&camera_description::is_primary>(meta::property_id{4}, "is_primary")(
                   property_edit_policy{.read_only = true})(
                   gneiss::serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  return meta::registration::type<mesh_component_description>()
      .base<component_description_base>()
      .property<&mesh_component_description::mesh_uri>(meta::property_id{1}, "mesh")(
          gneiss::serialization::field_policy{.required = true})
      .property<&mesh_component_description::material_uri>(meta::property_id{2}, "material")(
          gneiss::serialization::field_policy{.required = true})
      .metadata(
          component_validation<mesh_component_description, &validate_mesh_component_description>)
      .commit();
}
error register_builtin_resource_types() {
  auto status = register_component_descriptions();
  if (status.failed())
    return status;
  status = meta::registration::type<gneiss::texture>()
               .guid(guid::literal("601bc0bf-b255-4eb1-a51f-f06b59e3ff3d"))
               .name("texture")
               .metadata(serialization::reference_policy{
                   .encode = detail::encode_asset_reference<gneiss::texture>,
                   .decode = detail::decode_asset_reference<gneiss::texture>,
                   .capture = detail::capture_asset_reference<gneiss::texture>})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<gneiss::mesh>()
               .guid(guid::literal("3c7a40a9-3b99-4955-810e-819d3b160590"))
               .name("mesh")
               .metadata(serialization::reference_policy{
                   .encode = detail::encode_asset_reference<gneiss::mesh>,
                   .decode = detail::decode_asset_reference<gneiss::mesh>,
                   .capture = detail::capture_asset_reference<gneiss::mesh>})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<gneiss::material>()
               .guid(guid::literal("c8edeac7-022d-446f-972f-941b5d193b66"))
               .name("material")
               .metadata(serialization::reference_policy{
                   .encode = detail::encode_asset_reference<gneiss::material>,
                   .decode = detail::decode_asset_reference<gneiss::material>,
                   .capture = detail::capture_asset_reference<gneiss::material>})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<gneiss::scene>()
               .guid(guid::literal("83dc2409-ec41-4eec-85a1-c7563b421a71"))
               .name("scene")
               .metadata(serialization::reference_policy{
                   .encode = detail::encode_asset_reference<gneiss::scene>,
                   .decode = detail::decode_asset_reference<gneiss::scene>,
                   .capture = detail::capture_asset_reference<gneiss::scene>})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<gneiss::prefab>()
               .guid(guid::literal("114e020a-d5e0-4941-8d6c-a611caa26448"))
               .name("prefab")
               .metadata(serialization::reference_policy{
                   .encode = detail::encode_asset_reference<gneiss::prefab>,
                   .decode = detail::decode_asset_reference<gneiss::prefab>,
                   .capture = detail::capture_asset_reference<gneiss::prefab>})
               .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<mesh_description>()
          .property<&mesh_description::topology>(meta::property_id{1}, "topology")(
              gneiss::serialization::field_policy{.required = true})
          .property<&mesh_description::vertices>(meta::property_id{2}, "vertices")(
              gneiss::serialization::field_policy{.required = true})
          .property<&mesh_description::uvs>(meta::property_id{3}, "uvs")(
              gneiss::serialization::field_policy{.required = true})
          .property<&mesh_description::normals>(meta::property_id{4}, "normals")(
              gneiss::serialization::field_policy{.required = true})
          .metadata(gneiss::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status = meta::registration::enumeration<material_alpha_mode>()
               .value("opaque", material_alpha_mode::opaque)
               .value("mask", material_alpha_mode::mask)
               .value("blend", material_alpha_mode::blend)
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::enumeration<texture_filter>()
               .value("nearest", texture_filter::nearest)
               .value("linear", texture_filter::linear)
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::enumeration<texture_mip_filter>()
               .value("none", texture_mip_filter::none)
               .value("nearest", texture_mip_filter::nearest)
               .value("linear", texture_mip_filter::linear)
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::enumeration<texture_address>()
               .value("repeat", texture_address::repeat)
               .value("clamp", texture_address::clamp)
               .value("mirror", texture_address::mirror)
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::enumeration<texture_color_space>()
               .value("linear", texture_color_space::linear)
               .value("srgb", texture_color_space::srgb)
               .commit();
  if (status.failed())
    return status;
  status = meta::registration::enumeration<texture_usage>()
               .value("default", texture_usage::default_usage)
               .value("normal", texture_usage::normal)
               .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<texture_description>()
          .property<&texture_description::uri>(meta::property_id{1}, "source")(
              gneiss::serialization::field_policy{.required = true})
          .property<&texture_description::color_space>(meta::property_id{2}, "color_space")(
              gneiss::serialization::field_policy{.required = true, .enum_name = true})
          .property<&texture_description::usage>(meta::property_id{3}, "usage")(
              gneiss::serialization::field_policy{.required = true, .enum_name = true})
          .metadata(gneiss::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<texture_sampling>()
          .property<&texture_sampling::uv_set>(meta::property_id{1}, "uv_set")
          .property<&texture_sampling::mag_filter>(meta::property_id{2}, "mag_filter")
          .property<&texture_sampling::min_filter>(meta::property_id{3}, "min_filter")
          .property<&texture_sampling::mip_filter>(meta::property_id{4}, "mip_filter")
          .property<&texture_sampling::address_u>(meta::property_id{5}, "address_u")
          .property<&texture_sampling::address_v>(meta::property_id{6}, "address_v")
          .metadata(gneiss::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<material_parameters>()
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
          .metadata(gneiss::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<material_description>()
          .schema_version(6)
          .property<&material_description::parameters>(meta::property_id{1}, "parameters")(
              gneiss::serialization::field_policy{.required = true})
          .property<&material_description::texture_uris>(meta::property_id{2}, "texture_uris")(
              gneiss::serialization::field_policy{.required = true})
          .metadata(gneiss::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;

  status =
      meta::registration::type<input::action_binding>()
          .property<&input::action_binding::physical_key>(meta::property_id{1}, "key")(
              gneiss::serialization::field_policy{.required = true})
          .property<&input::action_binding::scale>(meta::property_id{2}, "scale")(
              gneiss::serialization::field_policy{.required = true})
          .metadata(gneiss::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<input::action_definition>()
          .property<&input::action_definition::name>(meta::property_id{1}, "name")(
              gneiss::serialization::field_policy{.required = true})
          .property<&input::action_definition::bindings>(meta::property_id{2}, "bindings")(
              gneiss::serialization::field_policy{.required = true})
          .metadata(gneiss::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration::type<input::action_map_description>()
          .schema_version(1)
          .property<&input::action_map_description::actions>(meta::property_id{1}, "actions")(
              gneiss::serialization::field_policy{.required = true})
          .metadata(gneiss::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status = meta::registration::type<scene_description>()
               .property<&scene_description::uuid>(meta::property_id{1}, "scene_uuid")(
                   gneiss::serialization::field_policy{.required = true})
               .property<&scene_description::objects>(meta::property_id{2}, "objects")(
                   gneiss::serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  return meta::registration::type<prefab_description>()
      .property<&prefab_description::uuid>(meta::property_id{1}, "prefab_uuid")(
          gneiss::serialization::field_policy{.required = true})
      .property<&prefab_description::root_uuid>(meta::property_id{2}, "root_uuid")(
          gneiss::serialization::field_policy{.required = true})
      .property<&prefab_description::objects>(meta::property_id{3}, "objects")(
          gneiss::serialization::field_policy{.required = true})
      .commit();
}
}
