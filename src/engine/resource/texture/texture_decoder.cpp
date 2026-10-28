// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "texture_decoder.hpp"
#include "engine/core/sha256.hpp"
#include "engine/resource/detail/asset_json.hpp"
#include "png_decoder.hpp"
#include "texture_binary.hpp"
#include "texture_ktx2.hpp"
#include <granit/renderer/texture_asset.hpp>
namespace gneiss::engine::detail {
[[nodiscard]] engine::result parse_texture(const std::vector<std::byte>& bytes,
                                           texture_description& out_source,
                                           asset_diagnostic& diagnostic) {
  auto document = parse_document(bytes, diagnostic);
  if (!document) {
    return diagnostic.result;
  }
  yyjson_val* root = yyjson_doc_get_root(document.get());
  constexpr std::array fields{
      std::string_view{"format"},      std::string_view{"version"}, std::string_view{"source"},
      std::string_view{"color_space"}, std::string_view{"usage"},
  };
  std::uint64_t version = 0;
  if (!validate_header(root, "gneiss.texture", fields, 2U, version, diagnostic)) {
    return diagnostic.result;
  }
  if (version != 2U) {
    fail(diagnostic, engine::result::unsupported, "/version", "纹理描述仅支持版本 2");
    return diagnostic.result;
  }
  yyjson_val* source = yyjson_obj_get(root, "source");
  if (!yyjson_is_str(source) || yyjson_get_len(source) == 0U) {
    fail(diagnostic, engine::result::invalid_argument, "/source", "Texture source 必须是非空 URI");
    return diagnostic.result;
  }
  yyjson_val* color_space = yyjson_obj_get(root, "color_space");
  if (!yyjson_is_str(color_space)) {
    fail(diagnostic, engine::result::invalid_argument, "/color_space",
         "Texture 颜色空间必须是字符串");
    return diagnostic.result;
  }
  const auto color_space_name = json_string(color_space);
  if (color_space_name == "srgb") {
    out_source.color_space = texture_color_space::srgb;
  } else if (color_space_name == "linear") {
    out_source.color_space = texture_color_space::linear;
  } else {
    fail(diagnostic, engine::result::invalid_argument, "/color_space", "只支持 srgb 或 linear");
    return diagnostic.result;
  }
  auto* const usage = yyjson_obj_get(root, "usage");
  if (!yyjson_is_str(usage) ||
      (json_string(usage) != "default" && json_string(usage) != "normal") ||
      (json_string(usage) == "normal" && out_source.color_space != texture_color_space::linear)) {
    fail(diagnostic, engine::result::invalid_argument, "/usage",
         "纹理用途必须是 default 或 normal，法线纹理必须使用 linear");
    return diagnostic.result;
  }
  out_source.uri.assign(json_string(source));
  return engine::result::success;
}

result decode_texture_payload(const texture_description& source,
                              const std::vector<std::byte>& image_bytes,
                              prepared_texture_data& output, asset_diagnostic& out_diagnostic,
                              [[maybe_unused]] std::size_t input_limit, std::size_t output_limit,
                              texture_prepare_profile profile) noexcept {
  output = {};
  out_diagnostic = {};
  try {
    auto result = engine::result::success;
    [[maybe_unused]] const bool selected =
        profile.generation != 0U && std::string_view(source.uri).ends_with(".gneiss-texture");
    if (std::string_view(source.uri).ends_with(".gneiss-texture")) {
      texture_binary_view binary;
      std::string decode_message;
      if (engine::decode_texture_binary(image_bytes, binary, decode_message) !=
          texture_binary_result::success) {
        fail(out_diagnostic, engine::result::invalid_argument, "/source",
             decode_message.empty() ? "运行纹理封装检查失败" : decode_message);
        return engine::result::invalid_argument;
      }
      granit::texture_asset_info info;
      if (granit::inspect_texture_asset(binary.manifest, info) != granit::result::success ||
          info.dimension != granit::texture_dimension::two_dimensional || info.depth != 1U ||
          info.array_layers != 1U) {
        fail(out_diagnostic, engine::result::invalid_argument, "/source",
             "Granit Texture Asset Manifest 无效或不是二维单层纹理");
        return engine::result::invalid_argument;
      }
      const auto srgb = source.color_space == texture_color_space::srgb;
      const auto matches_color_space = [srgb](const auto& variant) {
        return srgb ? variant.format == granit::texture_format::bc7_rgba_srgb ||
                          variant.format == granit::texture_format::rgba8_srgb
                    : variant.format == granit::texture_format::bc7_rgba_unorm ||
                          variant.format == granit::texture_format::rgba8_unorm;
      };
      const auto payload_size = binary.payload.size();
      const auto payload_in_bounds = [payload_size](const auto& variant) {
        return variant.payload_offset <= payload_size &&
               variant.payload_size <= payload_size - variant.payload_offset;
      };
      const auto fallback_format =
          srgb ? granit::texture_format::rgba8_srgb : granit::texture_format::rgba8_unorm;
      const auto has_fallback =
          std::ranges::any_of(info.variants, [fallback_format](const auto& variant) {
            return variant.format == fallback_format;
          });
      if (!std::ranges::all_of(info.variants, matches_color_space) ||
          !std::ranges::all_of(info.variants, payload_in_bounds) || !has_fallback) {
        fail(out_diagnostic, engine::result::invalid_argument, "/source",
             "运行纹理变体颜色空间、负载边界或 RGBA8 回退无效");
        return engine::result::invalid_argument;
      }
      output = {
          .width = info.width,
          .height = info.height,
          .format = texture_format::rgba8_unorm,
          .color_space = source.color_space,
          .levels = {},
          .manifest = std::vector<std::byte>(binary.manifest.begin(), binary.manifest.end()),
          .payload = selected
                         ? std::vector<std::byte>{}
                         : std::vector<std::byte>(binary.payload.begin(), binary.payload.end()),
      };
      if (selected) {
        constexpr std::array formats{
            granit::texture_format::rgba8_unorm, granit::texture_format::rgba8_srgb,
            granit::texture_format::bc7_rgba_unorm, granit::texture_format::bc7_rgba_srgb};
        constexpr auto usage = static_cast<std::uint32_t>(
            granit::texture_usage::sampled | granit::texture_usage::transfer_destination);
        for (std::size_t index = 0U; index < info.variants.size(); ++index) {
          const auto& variant = info.variants[index];
          const auto format = std::ranges::find(formats, variant.format);
          if (format == formats.end() ||
              !profile
                   .sampled_transfer_formats[static_cast<std::size_t>(format - formats.begin())] ||
              (static_cast<std::uint32_t>(variant.usage) & usage) != usage) {
            continue;
          }
          if (output.manifest.size() > output_limit ||
              variant.payload_size > output_limit - output.manifest.size() ||
              variant.payload_size > input_limit) {
            const auto manifest_bytes = output.manifest.size();
            output = {};
            fail(out_diagnostic, engine::result::out_of_memory, "/source",
                 "纹理准备预算不足：所选负载需要 " + std::to_string(variant.payload_size) +
                     " 字节，Manifest 需要 " + std::to_string(manifest_bytes) +
                     " 字节，负载读取上限 " + std::to_string(input_limit) + " 字节，合计可用 " +
                     std::to_string(output_limit) + " 字节");
            return engine::result::out_of_memory;
          }
          output.payload.resize(static_cast<std::size_t>(variant.payload_size));
          const auto selected_bytes = binary.payload.subspan(
              static_cast<std::size_t>(variant.payload_offset), output.payload.size());
          std::copy(selected_bytes.begin(), selected_bytes.end(), output.payload.begin());
          if (result == engine::result::success &&
              engine::sha256(output.payload) != variant.payload_digest) {
            result = engine::result::invalid_argument;
          }
          if (result != engine::result::success) {
            output = {};
            fail(out_diagnostic, result, "/source", "选中纹理变体读取或摘要校验失败");
            return result;
          }
          output.profile = profile;
          output.selected_variant = static_cast<std::uint32_t>(index);
          break;
        }
        if (output.profile.generation == 0U) {
          output = {};
          fail(out_diagnostic, engine::result::unsupported, "/source",
               "设备不支持运行纹理的任何变体");
          return engine::result::unsupported;
        }
      }
    } else if (std::string_view(source.uri).ends_with(".ktx2")) {
      texture_ktx2 texture;
      std::string decode_message;
      const auto decoded = engine::decode_texture_ktx2(image_bytes, texture, decode_message);
      if (decoded != texture_ktx2_result::success) {
        fail(out_diagnostic, engine::result::invalid_argument, "/source",
             decode_message.empty() ? "KTX2 解码失败" : decode_message);
        return engine::result::invalid_argument;
      }
      const auto expected_transfer = source.color_space == texture_color_space::srgb
                                         ? texture_transfer::srgb
                                         : texture_transfer::linear;
      if (texture.transfer != expected_transfer) {
        fail(out_diagnostic, engine::result::invalid_argument, "/color_space",
             "Texture 描述与 KTX2 传递函数不一致");
        return engine::result::invalid_argument;
      }
      auto levels = std::move(texture.levels);
      const auto width = levels.front().width;
      const auto height = levels.front().height;
      output = {.width = width,
                .height = height,
                .format = texture_format::rgba8_unorm,
                .color_space = source.color_space,
                .levels = std::move(levels),
                .manifest = {},
                .payload = {}};
    } else {
      decoded_png image;
      std::string decode_message;
      result = engine::decode_png(image_bytes, image, decode_message, output_limit);
      if (result != engine::result::success) {
        fail(out_diagnostic, result, "/source",
             decode_message.empty() ? "PNG 解码失败" : decode_message);
        return result;
      }
      output = {.width = image.width,
                .height = image.height,
                .format = texture_format::rgba8_unorm,
                .color_space = source.color_space,
                .levels = {},
                .manifest = {},
                .payload = {}};
      output.levels.push_back(
          {.width = image.width, .height = image.height, .pixels = std::move(image.pixels)});
    }

    return engine::result::success;
  } catch (const std::bad_alloc&) {
    output = {};
    return engine::result::out_of_memory;
  } catch (...) {
    output = {};
    return engine::result::internal;
  }
}
}
