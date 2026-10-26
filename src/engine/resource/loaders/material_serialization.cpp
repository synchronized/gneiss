// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "material_serialization.hpp"
#include "engine/core/meta/type_builder.hpp"
#include "engine/resource/loaders/material_description_decoder.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <yyjson.h>

namespace gneiss::engine {
result decode_material(std::span<const std::byte> bytes, material_description& output,
                       gneiss::engine::meta::serialization_diagnostic& diagnostic) noexcept try {
  diagnostic = {};
  std::vector<std::byte> owned(bytes.begin(), bytes.end());
  asset_diagnostic asset_error;
  const auto status = decode_material_description(owned, output, asset_error);
  diagnostic = {asset_error.byte_offset, std::move(asset_error.path),
                std::move(asset_error.message)};
  return status;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}

result encode_material(const material_description& object, std::vector<std::byte>& output,
                       gneiss::engine::meta::serialization_diagnostic& diagnostic) noexcept try {
  diagnostic = {};
  const auto& p = object.parameters;
  // JSON 不接受非有限数值；其余范围复用同一解析器校验，避免维护第二套格式规则。
  bool finite = std::isfinite(p.metallic) && std::isfinite(p.roughness) &&
                std::isfinite(p.normal_scale) && std::isfinite(p.occlusion_strength) &&
                std::isfinite(p.alpha_cutoff);
  for (float number : p.color)
    finite = finite && std::isfinite(number);
  for (float number : p.emissive)
    finite = finite && std::isfinite(number);
  if (!finite || p.alpha_mode > material_alpha_mode::blend) {
    diagnostic.message = "材质包含非有限数值或未知透明模式";
    return result::invalid_argument;
  }
  for (const auto& uri : object.texture_uris) {
    if (uri.find('\0') != std::string::npos) {
      diagnostic.message = "纹理 URI 不得包含空字符";
      return result::invalid_argument;
    }
  }
  using doc_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
  doc_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
  if (!document)
    return result::out_of_memory;
  auto* doc = document.get();
  auto* root = yyjson_mut_obj(doc);
  if (!root)
    return result::out_of_memory;
  yyjson_mut_doc_set_root(doc, root);
  const auto put = [&](const char* key, yyjson_mut_val* value) {
    if (!value || !yyjson_mut_obj_add_val(doc, root, key, value))
      throw std::bad_alloc{};
  };
  const auto floats = [&](const auto& values) {
    auto* array = yyjson_mut_arr(doc);
    if (!array)
      throw std::bad_alloc{};
    for (float value : values)
      if (!yyjson_mut_arr_add_real(doc, array, value))
        throw std::bad_alloc{};
    return array;
  };
  put("format", yyjson_mut_str(doc, "gneiss.material"));
  put("version", yyjson_mut_uint(doc, 5));
  put("color", floats(p.color));
  put("metallic", yyjson_mut_real(doc, p.metallic));
  put("roughness", yyjson_mut_real(doc, p.roughness));
  put("normal_scale", yyjson_mut_real(doc, p.normal_scale));
  put("occlusion_strength", yyjson_mut_real(doc, p.occlusion_strength));
  put("emissive", floats(p.emissive));
  constexpr std::array slots{"base_color_texture", "metallic_roughness_texture", "normal_texture",
                             "occlusion_texture", "emissive_texture"};
  for (std::size_t slot = 0; slot < slots.size(); ++slot) {
    const auto& uri = object.texture_uris[slot];
    put(slots[slot],
        uri.empty() ? yyjson_mut_null(doc) : yyjson_mut_strncpy(doc, uri.data(), uri.size()));
  }
  constexpr std::array modes{"OPAQUE", "MASK", "BLEND"};
  put("alpha_mode", yyjson_mut_str(doc, modes[static_cast<std::size_t>(p.alpha_mode)]));
  put("double_sided", yyjson_mut_bool(doc, p.double_sided));
  put("alpha_cutoff", yyjson_mut_real(doc, p.alpha_cutoff));
  auto* sampling = yyjson_mut_arr(doc);
  if (!sampling)
    return result::out_of_memory;
  for (const auto& item : p.sampling) {
    if (!valid_sampling(item)) {
      diagnostic.path = "/sampling";
      return result::invalid_argument;
    }
    auto* entry = yyjson_mut_obj(doc);
    if (!entry || !yyjson_mut_arr_append(sampling, entry) ||
        !yyjson_mut_obj_add_uint(doc, entry, "uv_set", item.uv_set) ||
        !yyjson_mut_obj_add_uint(doc, entry, "mag_filter",
                                 static_cast<std::uint32_t>(item.mag_filter)) ||
        !yyjson_mut_obj_add_uint(doc, entry, "min_filter",
                                 static_cast<std::uint32_t>(item.min_filter)) ||
        !yyjson_mut_obj_add_uint(doc, entry, "mip_filter",
                                 static_cast<std::uint32_t>(item.mip_filter)) ||
        !yyjson_mut_obj_add_uint(doc, entry, "address_u",
                                 static_cast<std::uint32_t>(item.address_u)) ||
        !yyjson_mut_obj_add_uint(doc, entry, "address_v",
                                 static_cast<std::uint32_t>(item.address_v)))
      return result::out_of_memory;
  }
  put("sampling", sampling);
  std::size_t size{};
  yyjson_write_err error{};
  using text_ptr = std::unique_ptr<char, decltype(&std::free)>;
  text_ptr text(yyjson_mut_write_opts(doc, YYJSON_WRITE_NOFLAG, nullptr, &size, &error),
                &std::free);
  if (!text)
    return error.code == YYJSON_WRITE_ERROR_MEMORY_ALLOCATION ? result::out_of_memory
                                                              : result::invalid_argument;
  std::vector<std::byte> pending(size);
  std::memcpy(pending.data(), text.get(), size);
  material_description checked;
  const auto status = decode_material(pending, checked, diagnostic);
  if (status.failed())
    return status;
  output = std::move(pending);
  return result::success;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}

result register_material_reflection(const gneiss::engine::meta::registry_ref& registry) noexcept {
  return gneiss::engine::meta::type_builder<material_description>{registry}
      .serialization<&encode_material, &decode_material>()
      .commit();
}
}
