// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "material_codec.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/serialization.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <yyjson.h>
namespace gneiss::engine {
namespace {
using diagnostic = meta::serialization_diagnostic;
using document = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using mutable_document = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
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
result<std::vector<std::byte>, diagnostic> write_json(yyjson_mut_doc* doc) {
  std::size_t length{};
  std::unique_ptr<char, decltype(&std::free)> text(yyjson_mut_write(doc, 0, &length), &std::free);
  if (!text)
    return diagnostic{.error = error::out_of_memory};
  const auto bytes = std::as_bytes(std::span{text.get(), length});
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}
result<material_description, diagnostic> decode(std::span<const std::byte> bytes) {
  yyjson_read_err err{};
  document doc(yyjson_read_opts(reinterpret_cast<char*>(const_cast<std::byte*>(bytes.data())),
                                bytes.size(), 0, nullptr, &err),
               &yyjson_doc_free);
  if (!doc)
    return diagnostic{err.pos, "", err.msg ? err.msg : "JSON 语法错误",
                      err.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION ? error::out_of_memory
                                                                      : error::invalid_argument};
  auto* root = yyjson_doc_get_root(doc.get());
  if (!yyjson_is_obj(root))
    return invalid("", "材质根必须是对象");
  auto* format = yyjson_obj_get(root, "format");
  auto* version = yyjson_obj_get(root, "version");
  if (!yyjson_is_str(format) ||
      std::string_view(yyjson_get_str(format), yyjson_get_len(format)) != "gneiss.material")
    return invalid("/format", "资产格式标识错误");
  if (!yyjson_is_uint(version) || yyjson_get_uint(version) != 6)
    return diagnostic{.path = "/version",
                      .message = "材质仅支持版本 6",
                      .error = yyjson_is_uint(version) && yyjson_get_uint(version)
                                   ? error::unsupported
                                   : error::invalid_argument};
  std::size_t formats = 0, versions = 0;
  auto it = yyjson_obj_iter_with(root);
  while (auto* key = yyjson_obj_iter_next(&it)) {
    const std::string_view name(yyjson_get_str(key), yyjson_get_len(key));
    formats += name == "format";
    versions += name == "version";
  }
  if (formats != 1 || versions != 1)
    return invalid("", "重复格式头");
  mutable_document fields(yyjson_doc_mut_copy(doc.get(), nullptr), &yyjson_mut_doc_free);
  if (!fields)
    return diagnostic{.error = error::out_of_memory};
  auto* fields_root = yyjson_mut_doc_get_root(fields.get());
  (void)yyjson_mut_obj_remove_key(fields_root, "format");
  (void)yyjson_mut_obj_remove_key(fields_root, "version");
  auto content = write_json(fields.get());
  if (!content)
    return std::move(content).error();
  return meta::deserialize_reflected<material_description>(*content);
}
result<std::vector<std::byte>, diagnostic> encode(const material_description& object) {
  auto fields = meta::serialize_reflected(object);
  if (!fields)
    return std::move(fields).error();
  document doc(yyjson_read(reinterpret_cast<const char*>(fields->data()), fields->size(), 0),
               &yyjson_doc_free);
  if (!doc)
    return diagnostic{.error = error::out_of_memory};
  mutable_document output(yyjson_doc_mut_copy(doc.get(), nullptr), &yyjson_mut_doc_free);
  if (!output)
    return diagnostic{.error = error::out_of_memory};
  auto* root = yyjson_mut_doc_get_root(output.get());
  if (!yyjson_mut_obj_add_str(output.get(), root, "format", "gneiss.material") ||
      !yyjson_mut_obj_add_uint(output.get(), root, "version", 6))
    return diagnostic{.error = error::out_of_memory};
  return write_json(output.get());
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
