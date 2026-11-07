// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "builtin_resource_registration.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/resource/asset_serialization.hpp"
#include "engine/resource/input/action_map.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/material.hpp"
#include "engine/resource/mesh/mesh_description.hpp"
#include "engine/resource/mesh.hpp"
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
  auto base_status = meta::type_builder<component_description_base>{}.polymorphic().commit();
  if (base_status.failed())
    return base_status;
  constexpr auto required = meta::property_flags::required;
  auto status = meta::type_builder<prefab_property_target>{}
                    .property<&prefab_property_target::source_node_uuid>(
                        meta::property_id{1}, "source_node_uuid", required)
                    .property<&prefab_property_target::target_type_guid>(
                        meta::property_id{2}, "target_type_guid", required)
                    .serialization(true)
                    .commit();
  if (status.failed())
    return status;
  status =
      meta::type_builder<prefab_property_override_key>{}
          .property<&prefab_property_override_key::target>(meta::property_id{1}, "target", required)
          .property<&prefab_property_override_key::path>(meta::property_id{2}, "path", required)
          .serialization(true)
          .validate<&validate_property_override_key>()
          .commit();
  if (status.failed())
    return status;
  status = meta::type_builder<prefab_property_override>{}
               .property<&prefab_property_override::key>(meta::property_id{1}, "key", required)
               .property<&prefab_property_override::operation>(meta::property_id{2}, "operation",
                                                               required)
               .serialization(true)
               .commit();
  if (status.failed())
    return status;
  status = meta::type_builder<prefab_component_override>{}
               .property<&prefab_component_override::source_node_uuid>(meta::property_id{1},
                                                                       "source_node_uuid", required)
               .property<&prefab_component_override::type_guid>(meta::property_id{2}, "type_guid",
                                                                required)
               .property<&prefab_component_override::added>(meta::property_id{3}, "added", required)
               .serialization(true)
               .validate<&validate_component_override>()
               .commit();
  if (status.failed())
    return status;
  status = meta::type_builder<prefab_reference_description>{}
               .property<&prefab_reference_description::uri>(meta::property_id{1}, "uri", required)
               .property<&prefab_reference_description::overrides>(meta::property_id{2},
                                                                   "overrides", required)
               .property<&prefab_reference_description::component_overrides>(meta::property_id{3},
                                                                             "component_overrides")
               .serialization()
               .validate<&validate_prefab_reference_description>()
               .commit();
  if (status.failed())
    return status;
  status =
      meta::type_builder<object_description>{}
          .property<&object_description::uuid>(meta::property_id{1}, "uuid", required)
          .property<&object_description::name>(meta::property_id{2}, "name")
          .property<&object_description::parent_uuid>(meta::property_id{3}, "parent", required)
          .property<&object_description::transform>(meta::property_id{4}, "transform", required)
          .property<&object_description::components>(meta::property_id{5}, "components")
          .property<&object_description::prefab>(meta::property_id{6}, "prefab")
          .serialization()
          .validate<&validate_object_description>()
          .commit();
  if (status.failed())
    return status;
  status = meta::type_builder<math::camera_settings>{}
               .property<&math::camera_settings::fov>(meta::property_id{1}, "fov", required)
               .property<&math::camera_settings::near>(meta::property_id{2}, "near", required)
               .property<&math::camera_settings::far>(meta::property_id{3}, "far", required)
               .serialization()
               .validate<&validate_camera_description>()
               .commit();
  if (status.failed())
    return status;
  status =
      meta::type_builder<camera_description>{}
          .base<component_description_base>()
          .property<&camera_description::settings>(meta::property_id{1}, "settings", required)
          .property<&camera_description::is_primary>(
              meta::property_id{4}, "is_primary", required | meta::property_flags::editor_read_only)
          .serialization()
          .commit();
  if (status.failed())
    return status;
  return meta::type_builder<mesh_renderer_description>{}
      .base<component_description_base>()
      .property<&mesh_renderer_description::mesh_uri>(meta::property_id{1}, "mesh", required)
      .property<&mesh_renderer_description::material_uri>(meta::property_id{2}, "material",
                                                          required)
      .serialization()
      .validate<&validate_mesh_renderer_description>()
      .commit();
}
error register_builtin_resource_types() noexcept {
  auto status = register_component_descriptions();
  if (status.failed())
    return status;
  status = meta::register_type<engine::texture>(
      {.guid = guid::literal("601bc0bf-b255-4eb1-a51f-f06b59e3ff3d"),
       .schema_version = 1,
       .name = "texture",
       .serialization = {.encode_reference = detail::encode_asset_reference<engine::texture>,
                         .decode_reference = detail::decode_asset_reference<engine::texture>,
                         .capture_reference = detail::capture_asset_reference<engine::texture>}});
  if (status.failed())
    return status;
  status = meta::register_type<engine::mesh>(
      {.guid = guid::literal("3c7a40a9-3b99-4955-810e-819d3b160590"),
       .schema_version = 1,
       .name = "mesh",
       .serialization = {.encode_reference = detail::encode_asset_reference<engine::mesh>,
                         .decode_reference = detail::decode_asset_reference<engine::mesh>,
                         .capture_reference = detail::capture_asset_reference<engine::mesh>}});
  if (status.failed())
    return status;
  status = meta::register_type<engine::material>(
      {.guid = guid::literal("c8edeac7-022d-446f-972f-941b5d193b66"),
       .schema_version = 1,
       .name = "material",
       .serialization = {.encode_reference = detail::encode_asset_reference<engine::material>,
                         .decode_reference = detail::decode_asset_reference<engine::material>,
                         .capture_reference = detail::capture_asset_reference<engine::material>}});
  if (status.failed())
    return status;
  status = meta::type_builder<mesh_description>{}
               .property<&mesh_description::topology>(meta::property_id{1}, "topology",
                                                      meta::property_flags::required)
               .property<&mesh_description::vertices>(meta::property_id{2}, "vertices",
                                                      meta::property_flags::required)
               .property<&mesh_description::uvs>(meta::property_id{3}, "uvs",
                                                 meta::property_flags::required)
               .property<&mesh_description::normals>(meta::property_id{4}, "normals",
                                                     meta::property_flags::required)
               .serialization(true)
               .validate<&validate_mesh>()
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
  status = meta::type_builder<texture_description>{}
               .property<&texture_description::uri>(meta::property_id{1}, "source",
                                                    meta::property_flags::required)
               .property<&texture_description::color_space>(meta::property_id{2}, "color_space",
                                                            meta::property_flags::required |
                                                                meta::property_flags::enum_name)
               .property<&texture_description::usage>(meta::property_id{3}, "usage",
                                                      meta::property_flags::required |
                                                          meta::property_flags::enum_name)
               .serialization(true)
               .validate<&validate_texture>()
               .commit();
  if (status.failed())
    return status;
  status = meta::type_builder<texture_sampling>{}
               .property<&texture_sampling::uv_set>(meta::property_id{1}, "uv_set")
               .property<&texture_sampling::mag_filter>(meta::property_id{2}, "mag_filter")
               .property<&texture_sampling::min_filter>(meta::property_id{3}, "min_filter")
               .property<&texture_sampling::mip_filter>(meta::property_id{4}, "mip_filter")
               .property<&texture_sampling::address_u>(meta::property_id{5}, "address_u")
               .property<&texture_sampling::address_v>(meta::property_id{6}, "address_v")
               .serialization(true)
               .validate<&validate_sampling>()
               .commit();
  if (status.failed())
    return status;
  status = meta::type_builder<material_parameters>{}
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
               .serialization(true)
               .validate<&validate_parameters>()
               .commit();
  if (status.failed())
    return status;
  status = meta::type_builder<material_description>{}
               .schema_version(6)
               .property<&material_description::parameters>(meta::property_id{1}, "parameters",
                                                            meta::property_flags::required)
               .property<&material_description::texture_uris>(meta::property_id{2}, "texture_uris",
                                                              meta::property_flags::required)
               .serialization(true)
               .commit();
  if (status.failed())
    return status;

  constexpr auto required = meta::property_flags::required;
  status = meta::type_builder<action_binding>{}
               .property<&action_binding::physical_key>(meta::property_id{1}, "key", required)
               .property<&action_binding::scale>(meta::property_id{2}, "scale", required)
               .serialization(true)
               .validate<&validate_binding>()
               .commit();
  if (status.failed())
    return status;
  status = meta::type_builder<action_definition>{}
               .property<&action_definition::name>(meta::property_id{1}, "name", required)
               .property<&action_definition::bindings>(meta::property_id{2}, "bindings", required)
               .serialization(true)
               .commit();
  if (status.failed())
    return status;
  status =
      meta::type_builder<action_map_description>{}
          .schema_version(1)
          .property<&action_map_description::actions>(meta::property_id{1}, "actions", required)
          .serialization(true)
          .validate<&validate_action_map>()
          .commit();
  if (status.failed())
    return status;
  status = meta::type_builder<scene_description>{}
               .property<&scene_description::uuid>(meta::property_id{1}, "scene_uuid",
                                                   meta::property_flags::required)
               .property<&scene_description::objects>(meta::property_id{2}, "objects",
                                                      meta::property_flags::required)
               .serialization()
               .validate<&validate_scene_description>()
               .commit();
  if (status.failed())
    return status;
  return meta::type_builder<prefab_description>{}
      .property<&prefab_description::uuid>(meta::property_id{1}, "prefab_uuid",
                                           meta::property_flags::required)
      .property<&prefab_description::root_uuid>(meta::property_id{2}, "root_uuid",
                                                meta::property_flags::required)
      .property<&prefab_description::objects>(meta::property_id{3}, "objects",
                                              meta::property_flags::required)
      .serialization()
      .validate<&validate_prefab_description>()
      .commit();
}
}
