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
#include <algorithm>
#include <cmath>
#include <limits>
namespace gneiss::engine {
namespace {
using diagnostic = serialization::diagnostic;

diagnostic invalid_description(std::string path, std::string message = {}) {
  return {.path = std::move(path), .message = std::move(message), .error = error::invalid_argument};
}
diagnostic validate_mesh(const mesh_description& v) {
  if (v.topology != "triangle_list")
    return invalid_description("/topology", "只支持 triangle_list");
  const auto count = v.vertices.size();
  if (count < 3 || count % 3 != 0 || count > std::numeric_limits<std::uint32_t>::max())
    return invalid_description("/vertices", "顶点必须组成非空三角形列表");
  if (v.uvs.size() != count)
    return invalid_description("/uvs", "UV 数量必须与顶点数量一致");
  if (v.normals.size() != count)
    return invalid_description("/normals", "法线数量必须与顶点数量一致");
  for (std::size_t i = 0; i < count; ++i) {
    const auto& n = v.normals[i];
    const auto length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (!std::isfinite(length) || std::abs(length - 1.0F) > 1.0e-4F)
      return invalid_description("/normals/" + std::to_string(i), "法线必须归一化");
  }
  return {};
}

diagnostic validate_sampling(const texture_sampling& v) {
  return valid_sampling(v) ? diagnostic{} : invalid_description("", "采样值越界");
}
diagnostic validate_parameters(const material_parameters& v) {
  const auto unit = [](float n) { return std::isfinite(n) && n >= 0 && n <= 1; };
  for (std::size_t i = 0; i < 4; ++i)
    if (!unit(v.color[i]))
      return invalid_description("/color/" + std::to_string(i));
  if (!unit(v.metallic))
    return invalid_description("/metallic");
  if (!unit(v.roughness))
    return invalid_description("/roughness");
  if (!unit(v.occlusion_strength))
    return invalid_description("/occlusion_strength");
  if (!std::isfinite(v.normal_scale))
    return invalid_description("/normal_scale");
  for (float n : math::components(v.emissive))
    if (!unit(n))
      return invalid_description("/emissive");
  if (!std::isfinite(v.alpha_cutoff) || v.alpha_cutoff < 0)
    return invalid_description("/alpha_cutoff");
  if (v.alpha_mode > material_alpha_mode::blend)
    return invalid_description("/alpha_mode");
  return {};
}
diagnostic validate_texture(const texture_description& v) {
  if (v.uri.empty())
    return invalid_description("/source", "纹理 source 必须是非空 URI");
  if (v.usage == texture_usage::normal && v.color_space != texture_color_space::linear)
    return invalid_description("/usage", "法线纹理必须使用 linear");
  return {};
}

diagnostic action_map_invalid(std::string path, std::string message = {}) {
  return {.path = std::move(path), .message = std::move(message), .error = error::invalid_argument};
}
diagnostic validate_binding(const action_binding& binding) {
  if (binding.physical_key == 0 || binding.physical_key >= 256 || !std::isfinite(binding.scale) ||
      binding.scale == 0 || binding.scale < -1 || binding.scale > 1)
    return action_map_invalid("", "按键或 scale 超出有效范围");
  return {};
}

}
error register_component_descriptions() noexcept {
  // 基类身份字段使用保留编号，派生组件业务字段从 1 开始。
  auto status =
      meta::registration<component_description_base>{}
          .metadata(gneiss::engine::serialization::reference_policy{.polymorphic = true})
          .property<&component_description_base::uuid>(meta::property_id{0xffffffffU}, "uuid")
          .property_metadata(meta::property_id{0xffffffffU},
                             property_edit_policy{.read_only = true})
          .property_metadata(meta::property_id{0xffffffffU},
                             gneiss::engine::serialization::field_policy{.required = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<prefab_property_target>{}
          .property<&prefab_property_target::source_node_uuid>(meta::property_id{1},
                                                               "source_node_uuid")
          .property_metadata(meta::property_id{1},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_property_target::target_type_guid>(meta::property_id{2},
                                                               "target_type_guid")
          .property_metadata(meta::property_id{2},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_property_target::component_uuid>(meta::property_id{3}, "component_uuid")
          .property_metadata(meta::property_id{3},
                             gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<prefab_property_override_key>{}
          .property<&prefab_property_override_key::target>(meta::property_id{1}, "target")
          .property_metadata(meta::property_id{1},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_property_override_key::path>(meta::property_id{2}, "path")
          .property_metadata(meta::property_id{2},
                             gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .extend(gneiss::engine::serialization::validation<&validate_property_override_key>)
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<prefab_property_override>{}
          .property<&prefab_property_override::key>(meta::property_id{1}, "key")
          .property_metadata(meta::property_id{1},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_property_override::operation>(meta::property_id{2}, "operation")
          .property_metadata(meta::property_id{2},
                             gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<prefab_component_override>{}
          .property<&prefab_component_override::source_node_uuid>(meta::property_id{1},
                                                                  "source_node_uuid")
          .property_metadata(meta::property_id{1},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_component_override::type_guid>(meta::property_id{2}, "type_guid")
          .property_metadata(meta::property_id{2},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_component_override::added>(meta::property_id{3}, "added")
          .property_metadata(meta::property_id{3},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_component_override::component_uuid>(meta::property_id{4},
                                                                "component_uuid")
          .property_metadata(meta::property_id{4},
                             gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .extend(gneiss::engine::serialization::validation<&validate_component_override>)
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<prefab_reference_description>{}
          .property<&prefab_reference_description::uri>(meta::property_id{1}, "uri")
          .property_metadata(meta::property_id{1},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_reference_description::overrides>(meta::property_id{2}, "overrides")
          .property_metadata(meta::property_id{2},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&prefab_reference_description::component_overrides>(meta::property_id{3},
                                                                        "component_overrides")
          .extend(gneiss::engine::serialization::validation<&validate_prefab_reference_description>)
          .commit();
  if (status.failed())
    return status;
  status = meta::registration<object_address>{}
               .property<&object_address::object>(meta::property_id{1}, "object")
               .property_metadata(meta::property_id{1},
                                  gneiss::engine::serialization::field_policy{.required = true})
               .property<&object_address::source>(meta::property_id{2}, "source")
               .property_metadata(meta::property_id{2},
                                  gneiss::engine::serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  status = meta::registration<game_object_description>{}
               .property<&game_object_description::uuid>(meta::property_id{1}, "uuid")
               .property_metadata(meta::property_id{1},
                                  gneiss::engine::serialization::field_policy{.required = true})
               .property<&game_object_description::name>(meta::property_id{2}, "name")
               .property<&game_object_description::parent>(meta::property_id{3}, "parent")
               .property_metadata(meta::property_id{3},
                                  gneiss::engine::serialization::field_policy{.required = true})
               .property<&game_object_description::transform>(meta::property_id{4}, "transform")
               .property_metadata(meta::property_id{4},
                                  gneiss::engine::serialization::field_policy{.required = true})
               .property<&game_object_description::components>(meta::property_id{5}, "components")
               .property<&game_object_description::prefab>(meta::property_id{6}, "prefab")
               .extend(gneiss::engine::serialization::validation<&validate_game_object_description>)
               .commit();
  if (status.failed())
    return status;
  status = meta::registration<camera_description>{}
               .base<component_description_base>()
               .property<&camera_description::settings>(meta::property_id{1}, "settings")
               .property_metadata(meta::property_id{1},
                                  gneiss::engine::serialization::field_policy{.required = true})
               .property<&camera_description::is_primary>(meta::property_id{4}, "is_primary")
               .property_metadata(meta::property_id{4}, property_edit_policy{.read_only = true})
               .property_metadata(meta::property_id{4},
                                  gneiss::engine::serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  return meta::registration<mesh_renderer_description>{}
      .base<component_description_base>()
      .property<&mesh_renderer_description::mesh_uri>(meta::property_id{1}, "mesh")
      .property_metadata(meta::property_id{1},
                         gneiss::engine::serialization::field_policy{.required = true})
      .property<&mesh_renderer_description::material_uri>(meta::property_id{2}, "material")
      .property_metadata(meta::property_id{2},
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
          .property<&mesh_description::topology>(meta::property_id{1}, "topology")
          .property_metadata(meta::property_id{1},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&mesh_description::vertices>(meta::property_id{2}, "vertices")
          .property_metadata(meta::property_id{2},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&mesh_description::uvs>(meta::property_id{3}, "uvs")
          .property_metadata(meta::property_id{3},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&mesh_description::normals>(meta::property_id{4}, "normals")
          .property_metadata(meta::property_id{4},
                             gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .extend(gneiss::engine::serialization::validation<&validate_mesh>)
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
          .property<&texture_description::uri>(meta::property_id{1}, "source")
          .property_metadata(meta::property_id{1},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&texture_description::color_space>(meta::property_id{2}, "color_space")
          .property_metadata(
              meta::property_id{2},
              gneiss::engine::serialization::field_policy{.required = true, .enum_name = true})
          .property<&texture_description::usage>(meta::property_id{3}, "usage")
          .property_metadata(
              meta::property_id{3},
              gneiss::engine::serialization::field_policy{.required = true, .enum_name = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .extend(gneiss::engine::serialization::validation<&validate_texture>)
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
          .extend(gneiss::engine::serialization::validation<&validate_sampling>)
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
          .extend(gneiss::engine::serialization::validation<&validate_parameters>)
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<material_description>{}
          .schema_version(6)
          .property<&material_description::parameters>(meta::property_id{1}, "parameters")
          .property_metadata(meta::property_id{1},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&material_description::texture_uris>(meta::property_id{2}, "texture_uris")
          .property_metadata(meta::property_id{2},
                             gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;

  status =
      meta::registration<action_binding>{}
          .property<&action_binding::physical_key>(meta::property_id{1}, "key")
          .property_metadata(meta::property_id{1},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&action_binding::scale>(meta::property_id{2}, "scale")
          .property_metadata(meta::property_id{2},
                             gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .extend(gneiss::engine::serialization::validation<&validate_binding>)
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<action_definition>{}
          .property<&action_definition::name>(meta::property_id{1}, "name")
          .property_metadata(meta::property_id{1},
                             gneiss::engine::serialization::field_policy{.required = true})
          .property<&action_definition::bindings>(meta::property_id{2}, "bindings")
          .property_metadata(meta::property_id{2},
                             gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .commit();
  if (status.failed())
    return status;
  status =
      meta::registration<action_map_description>{}
          .schema_version(1)
          .property<&action_map_description::actions>(meta::property_id{1}, "actions")
          .property_metadata(meta::property_id{1},
                             gneiss::engine::serialization::field_policy{.required = true})
          .metadata(gneiss::engine::serialization::object_policy{.reject_unknown_fields = true})
          .extend(gneiss::engine::serialization::validation<&validate_action_map>)
          .commit();
  if (status.failed())
    return status;
  status = meta::registration<scene_description>{}
               .property<&scene_description::uuid>(meta::property_id{1}, "scene_uuid")
               .property_metadata(meta::property_id{1},
                                  gneiss::engine::serialization::field_policy{.required = true})
               .property<&scene_description::objects>(meta::property_id{2}, "objects")
               .property_metadata(meta::property_id{2},
                                  gneiss::engine::serialization::field_policy{.required = true})
               .commit();
  if (status.failed())
    return status;
  return meta::registration<prefab_description>{}
      .property<&prefab_description::uuid>(meta::property_id{1}, "prefab_uuid")
      .property_metadata(meta::property_id{1},
                         gneiss::engine::serialization::field_policy{.required = true})
      .property<&prefab_description::root_uuid>(meta::property_id{2}, "root_uuid")
      .property_metadata(meta::property_id{2},
                         gneiss::engine::serialization::field_policy{.required = true})
      .property<&prefab_description::objects>(meta::property_id{3}, "objects")
      .property_metadata(meta::property_id{3},
                         gneiss::engine::serialization::field_policy{.required = true})
      .commit();
}
}
