// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "material_codec.hpp"
#include "engine/resource/detail/asset_json.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <yyjson.h>

namespace gneiss::engine::detail {
[[nodiscard]] gneiss::engine::result parse_pbr_extensions(yyjson_val* root,
                                                          material_description& out_source,
                                                          asset_diagnostic& diagnostic) {
  constexpr std::array names{
      "base_color_texture", "metallic_roughness_texture", "normal_texture",
      "occlusion_texture",  "emissive_texture",
  };
  for (std::size_t slot = 1; slot < names.size(); ++slot) {
    auto* texture = yyjson_obj_get(root, names[slot]);
    if (texture == nullptr || yyjson_is_null(texture)) {
      continue;
    }
    if (!yyjson_is_str(texture) || yyjson_get_len(texture) == 0U) {
      fail(diagnostic, gneiss::engine::result::invalid_argument, std::string{"/"} + names[slot],
           "Texture 必须是非空 URI 或 null");
      return diagnostic.result;
    }
    out_source.texture_uris[slot].assign(json_string(texture));
  }
  auto* normal_scale = yyjson_obj_get(root, "normal_scale");
  auto* strength = yyjson_obj_get(root, "occlusion_strength");
  if ((normal_scale != nullptr && !read_float(normal_scale, out_source.parameters.normal_scale)) ||
      (strength != nullptr && (!read_float(strength, out_source.parameters.occlusion_strength) ||
                               out_source.parameters.occlusion_strength < 0.0F ||
                               out_source.parameters.occlusion_strength > 1.0F))) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/normal_scale",
         "法线缩放必须有限，AO 强度必须位于 0..1");
    return diagnostic.result;
  }
  if (auto* emissive = yyjson_obj_get(root, "emissive")) {
    if (!yyjson_is_arr(emissive) || yyjson_arr_size(emissive) != 3U) {
      fail(diagnostic, gneiss::engine::result::invalid_argument, "/emissive",
           "自发光必须包含三个分量");
      return diagnostic.result;
    }
    for (std::size_t i = 0; i < 3U; ++i) {
      if (!read_float(yyjson_arr_get(emissive, i), out_source.parameters.emissive[i]) ||
          out_source.parameters.emissive[i] < 0.0F || out_source.parameters.emissive[i] > 1.0F) {
        fail(diagnostic, gneiss::engine::result::invalid_argument, "/emissive",
             "自发光分量必须位于 0..1");
        return diagnostic.result;
      }
    }
  }
  return gneiss::engine::result::success;
}

[[nodiscard]] bool parse_material_sampling(yyjson_val* root,
                                           gneiss::engine::material_parameters& state) {
  if (auto* sampling = yyjson_obj_get(root, "sampling")) {
    if (!yyjson_is_arr(sampling) || yyjson_arr_size(sampling) != 5U) {
      return false;
    }
    constexpr std::array names{
        "uv_set", "mag_filter", "min_filter", "mip_filter", "address_u", "address_v",
    };
    constexpr std::array limits{1U, 1U, 1U, 2U, 2U, 2U};
    for (std::size_t slot = 0; slot < 5U; ++slot) {
      auto* item = yyjson_arr_get(sampling, slot);
      if (!yyjson_is_obj(item) || yyjson_obj_size(item) != names.size()) {
        return false;
      }
      auto& sample = state.sampling[slot];
      std::array<std::uint32_t, 6> fields{};
      for (std::size_t field = 0; field < names.size(); ++field) {
        auto* value = yyjson_obj_get(item, names[field]);
        if (!yyjson_is_uint(value) || yyjson_get_uint(value) > limits[field]) {
          return false;
        }
        fields[field] = static_cast<std::uint32_t>(yyjson_get_uint(value));
      }
      sample = {fields[0],
                static_cast<gneiss::engine::texture_filter>(fields[1]),
                static_cast<gneiss::engine::texture_filter>(fields[2]),
                static_cast<gneiss::engine::texture_mip_filter>(fields[3]),
                static_cast<gneiss::engine::texture_address>(fields[4]),
                static_cast<gneiss::engine::texture_address>(fields[5])};
    }
  }
  return true;
}

[[nodiscard]] bool parse_material_state(yyjson_val* root,
                                        gneiss::engine::material_parameters& state) {
  if (auto* mode = yyjson_obj_get(root, "alpha_mode")) {
    if (!yyjson_is_str(mode)) {
      return false;
    }
    const auto name = json_string(mode);
    if (name == "OPAQUE") {
      state.alpha_mode = gneiss::engine::material_alpha_mode::opaque;
    } else if (name == "MASK") {
      state.alpha_mode = gneiss::engine::material_alpha_mode::mask;
    } else if (name == "BLEND") {
      state.alpha_mode = gneiss::engine::material_alpha_mode::blend;
    } else {
      return false;
    }
  }
  if (auto* sided = yyjson_obj_get(root, "double_sided")) {
    if (!yyjson_is_bool(sided)) {
      return false;
    }
    state.double_sided = yyjson_get_bool(sided);
  }
  if (auto* cutoff = yyjson_obj_get(root, "alpha_cutoff");
      cutoff && (!read_float(cutoff, state.alpha_cutoff) || state.alpha_cutoff < 0.0F)) {
    return false;
  }

  return parse_material_sampling(root, state);
}

[[nodiscard]] gneiss::engine::result parse_material(const std::vector<std::byte>& bytes,
                                                    material_description& out_source,
                                                    asset_diagnostic& diagnostic) {
  auto document = parse_document(bytes, diagnostic);
  if (!document) {
    return diagnostic.result;
  }
  yyjson_val* root = yyjson_doc_get_root(document.get());
  constexpr std::array fields{
      std::string_view{"format"},
      std::string_view{"version"},
      std::string_view{"color"},
      std::string_view{"base_color_texture"},
      std::string_view{"metallic"},
      std::string_view{"roughness"},
      std::string_view{"metallic_roughness_texture"},
      std::string_view{"normal_texture"},
      std::string_view{"occlusion_texture"},
      std::string_view{"emissive_texture"},
      std::string_view{"normal_scale"},
      std::string_view{"occlusion_strength"},
      std::string_view{"emissive"},
      std::string_view{"alpha_mode"},
      std::string_view{"double_sided"},
      std::string_view{"alpha_cutoff"},
      std::string_view{"sampling"},
  };
  std::uint64_t version{};
  if (!validate_header(root, "gneiss.material", fields, 5U, version, diagnostic))
    return diagnostic.result;
  if (version != 5U) {
    fail(diagnostic, result::unsupported, "/version", "材质仅支持版本 5");
    return diagnostic.result;
  }
  yyjson_val* color = yyjson_obj_get(root, "color");
  if (!yyjson_is_arr(color) || yyjson_arr_size(color) != out_source.parameters.color.size()) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/color", "颜色必须包含四个分量");
    return diagnostic.result;
  }
  for (std::size_t index = 0; index < out_source.parameters.color.size(); ++index) {
    if (!read_float(yyjson_arr_get(color, index), out_source.parameters.color[index]) ||
        out_source.parameters.color[index] < 0.0F || out_source.parameters.color[index] > 1.0F) {
      fail(diagnostic, gneiss::engine::result::invalid_argument, "/color/" + std::to_string(index),
           "颜色分量必须位于 0..1");
      return diagnostic.result;
    }
  }
  {
    yyjson_val* texture = yyjson_obj_get(root, "base_color_texture");
    if (yyjson_is_null(texture)) {
      out_source.texture_uris[0].clear();
    } else if (!yyjson_is_str(texture) || yyjson_get_len(texture) == 0U) {
      fail(diagnostic, gneiss::engine::result::invalid_argument, "/base_color_texture",
           "base-color Texture 必须是非空 URI");
      return diagnostic.result;
    } else {
      out_source.texture_uris[0].assign(json_string(texture));
    }
  }
  if (!read_float(yyjson_obj_get(root, "metallic"), out_source.parameters.metallic) ||
      out_source.parameters.metallic < 0.0F || out_source.parameters.metallic > 1.0F ||
      !read_float(yyjson_obj_get(root, "roughness"), out_source.parameters.roughness) ||
      out_source.parameters.roughness < 0.0F || out_source.parameters.roughness > 1.0F) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/metallic",
         "metallic 与 roughness 必须位于 0..1");
    return diagnostic.result;
  }

  if (parse_pbr_extensions(root, out_source, diagnostic) != gneiss::engine::result::success) {
    return diagnostic.result;
  }
  if (!parse_material_state(root, out_source.parameters)) {
    fail(diagnostic, gneiss::engine::result::invalid_argument, "/sampling",
         "材质状态或逐槽采样设置无效");
    return diagnostic.result;
  }
  return gneiss::engine::result::success;
}

}
namespace gneiss::engine {
/** 复用批次已经读取的字节；不进行 I/O、来源复验或共享版本登记；解析失败保持原输出。 */
gneiss::engine::result decode_material_description(const std::vector<std::byte>& bytes,
                                                   material_description& value,
                                                   asset_diagnostic& diagnostic) {
  material_description pending;
  const auto status = gneiss::engine::detail::parse_material(bytes, pending, diagnostic);
  if (status == result::success)
    value = std::move(pending);
  return status;
}

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

}
