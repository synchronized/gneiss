// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "material_codec.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include "engine/resource/detail/reflected_asset_codec.hpp"
#include <algorithm>
#include <cmath>
namespace gneiss::engine {
namespace {
using diagnostic = meta::serialization_diagnostic;
diagnostic invalid(std::string path, std::string message = {}) {
  return {.path = std::move(path), .message = std::move(message), .error = error::invalid_argument};
}
diagnostic validate_sampling(const texture_sampling& v) {
  return valid_sampling(v) ? diagnostic{} : invalid("", "采样值越界");
}
diagnostic validate_parameters(const material_parameters& v) {
  const auto unit = [](float n) { return std::isfinite(n) && n >= 0 && n <= 1; };
  for (std::size_t i = 0; i < 4; ++i)
    if (!unit(v.color[i]))
      return invalid("/color/" + std::to_string(i));
  if (!unit(v.metallic))
    return invalid("/metallic");
  if (!unit(v.roughness))
    return invalid("/roughness");
  if (!unit(v.occlusion_strength))
    return invalid("/occlusion_strength");
  if (!std::isfinite(v.normal_scale))
    return invalid("/normal_scale");
  for (float n : math::components(v.emissive))
    if (!unit(n))
      return invalid("/emissive");
  if (!std::isfinite(v.alpha_cutoff) || v.alpha_cutoff < 0)
    return invalid("/alpha_cutoff");
  if (v.alpha_mode > material_alpha_mode::blend)
    return invalid("/alpha_mode");
  return {};
}
constexpr detail::reflected_asset_format file_format{"gneiss.material", 6};
result<material_description, diagnostic> decode(std::span<const std::byte> bytes) {
  return detail::decode_reflected_asset<material_description>(bytes, file_format);
}
result<std::vector<std::byte>, diagnostic> encode(const material_description& object) {
  return detail::encode_reflected_asset(object, file_format);
}
}
error register_material_reflection() noexcept {
  auto status = meta::enum_builder<material_alpha_mode>{}
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
  return meta::type_builder<material_description>{}
      .schema_version(6)
      .property<&material_description::parameters>(meta::property_id{1}, "parameters",
                                                   meta::property_flags::required)
      .property<&material_description::texture_uris>(meta::property_id{2}, "texture_uris",
                                                     meta::property_flags::required)
      .serialization(true)
      .serialization<&encode, &decode>()
      .commit();
}
result<material_description, diagnostic>
decode_material(std::span<const std::byte> bytes) noexcept {
  return meta::deserialize<material_description>(bytes);
}
result<std::vector<std::byte>, diagnostic>
encode_material(const material_description& object) noexcept {
  return meta::serialize(object);
}
}
