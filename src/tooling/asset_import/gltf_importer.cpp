// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/gltf_importer.h"

#include "asset/mesh_tangent.h"
#include "tooling/asset_import/tangent_generation.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gneiss::tooling::asset_import {
namespace {

[[nodiscard]] inspect_report failure(inspect_result result, std::string diagnostic) {
  inspect_report report;
  report.result = result;
  report.diagnostic = std::move(diagnostic);
  return report;
}

[[nodiscard]] std::string read_failure_detail(std::string_view detail) {
  std::string message{"无法读取 glTF："};
  message.append(detail);
  return message;
}

[[nodiscard]] std::string parse_failure_detail(std::string_view detail) {
  std::string message{"glTF 解析失败："};
  message.append(detail);
  return message;
}

[[nodiscard]] bool checked_add(std::size_t left, std::size_t right, std::size_t& result) {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

[[nodiscard]] bool accessors_fit_buffers(const fastgltf::Asset& asset) {
  for (const auto& view : asset.bufferViews) {
    std::size_t view_end = 0;
    if (view.bufferIndex >= asset.buffers.size() ||
        !checked_add(view.byteOffset, view.byteLength, view_end) ||
        view_end > asset.buffers[view.bufferIndex].byteLength) {
      return false;
    }
  }

  for (const auto& accessor : asset.accessors) {
    if (!accessor.bufferViewIndex) {
      continue;
    }
    if (*accessor.bufferViewIndex >= asset.bufferViews.size()) {
      return false;
    }
    const auto& view = asset.bufferViews[*accessor.bufferViewIndex];
    const auto element_size = fastgltf::getElementByteSize(accessor.type, accessor.componentType);
    const auto stride = view.byteStride.value_or(element_size);
    if (stride < element_size) {
      return false;
    }
    std::size_t last_offset = accessor.byteOffset;
    if (accessor.count > 0U &&
        ((accessor.count - 1U) > (std::numeric_limits<std::size_t>::max() - last_offset) / stride ||
         !checked_add(last_offset, (accessor.count - 1U) * stride, last_offset) ||
         !checked_add(last_offset, element_size, last_offset))) {
      return false;
    }
    if (last_offset > view.byteLength) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool is_finite(const fastgltf::math::fvec3& value);

[[nodiscard]] fastgltf::MimeType image_mime_type(const fastgltf::Image& image) {
  return std::visit(
      [](const auto& source) {
        if constexpr (requires { source.mimeType; }) {
          if (source.mimeType != fastgltf::MimeType::None) {
            return source.mimeType;
          }
        }
        // 外部图片 URI 可以省略 MIME；依据已读取的签名识别，不相信文件扩展名。
        if constexpr (requires { source.bytes; }) {
          constexpr std::array signature{std::byte{137}, std::byte{80}, std::byte{78},
                                         std::byte{71},  std::byte{13}, std::byte{10},
                                         std::byte{26},  std::byte{10}};
          const auto bytes = std::span(source.bytes.data(), source.bytes.size());
          if (bytes.size() >= signature.size() &&
              std::ranges::equal(bytes.first(signature.size()), signature)) {
            return fastgltf::MimeType::PNG;
          }
        }
        return fastgltf::MimeType::None;
      },
      image.data);
}

void copy_image_bytes(const fastgltf::Asset& asset, const fastgltf::Image& image,
                      import_ir_image& target) {
  std::visit(fastgltf::visitor{
                 [&](const fastgltf::sources::BufferView& source) {
                   const auto bytes =
                       fastgltf::DefaultBufferDataAdapter{}(asset, source.bufferViewIndex);
                   target.bytes.assign(bytes.begin(), bytes.end());
                 },
                 [&](const auto& source) {
                   if constexpr (requires { source.bytes; }) {
                     target.bytes.assign(source.bytes.begin(), source.bytes.end());
                   }
                 },
             },
             image.data);
}

[[nodiscard]] bool safe_external_uri(const fastgltf::DataSource& source,
                                     const std::filesystem::path& source_directory) {
  const auto* uri_source = std::get_if<fastgltf::sources::URI>(&source);
  if (uri_source == nullptr || uri_source->uri.isDataUri()) {
    return true;
  }
  if (!uri_source->uri.isLocalPath()) {
    return false;
  }
  const auto path = uri_source->uri.fspath();
  if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
    return false;
  }
  const auto normalized = path.lexically_normal();
  if (std::ranges::any_of(normalized, [](const auto& component) { return component == ".."; })) {
    return false;
  }
  std::error_code error;
  const auto canonical_source = std::filesystem::weakly_canonical(source_directory, error);
  if (error) {
    return false;
  }
  const auto canonical_resource =
      std::filesystem::weakly_canonical(source_directory / normalized, error);
  if (error) {
    return false;
  }
  const auto relative = canonical_resource.lexically_relative(canonical_source);
  return !relative.empty() &&
         std::ranges::none_of(relative, [](const auto& component) { return component == ".."; });
}

[[nodiscard]] bool asset_uris_are_safe(const fastgltf::Asset& asset,
                                       const std::filesystem::path& source_directory) {
  return std::ranges::all_of(asset.buffers,
                             [&](const auto& buffer) {
                               return safe_external_uri(buffer.data, source_directory);
                             }) &&
         std::ranges::all_of(asset.images, [&](const auto& image) {
           return safe_external_uri(image.data, source_directory);
         });
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
[[nodiscard]] std::optional<std::string> validate_supported_scope(const fastgltf::Asset& asset,
                                                                  std::size_t& primitive_count) {
  if (!asset.extensionsRequired.empty()) {
    return "首版不支持 glTF 必需扩展";
  }
  if (!asset.animations.empty() || !asset.skins.empty()) {
    return "首版只支持静态场景";
  }
  for (const auto& node : asset.nodes) {
    if (!std::holds_alternative<fastgltf::TRS>(node.transform)) {
      return "节点矩阵包含无法分解的倾斜或透视变换";
    }
    const auto& transform = std::get<fastgltf::TRS>(node.transform);
    float rotation_length_squared = 0.0F;
    for (std::size_t index = 0; index < 4U; ++index) {
      rotation_length_squared += transform.rotation[index] * transform.rotation[index];
    }
    if (!is_finite(transform.translation) || !is_finite(transform.scale) ||
        !std::isfinite(rotation_length_squared) || rotation_length_squared <= 0.0F ||
        std::abs(transform.scale[0]) < 1.0e-6F || std::abs(transform.scale[1]) < 1.0e-6F ||
        std::abs(transform.scale[2]) < 1.0e-6F) {
      return "节点 Transform 包含无效数值、零缩放或无效旋转";
    }
  }
  for (const auto& material : asset.materials) {
    for (std::size_t index = 0; index < 4U; ++index) {
      if (!std::isfinite(material.pbrData.baseColorFactor[index])) {
        return "材质基础颜色包含非有限数值";
      }
    }
    const std::array<const fastgltf::TextureInfo*, 5> textures{
        material.pbrData.baseColorTexture ? &*material.pbrData.baseColorTexture : nullptr,
        material.pbrData.metallicRoughnessTexture ? &*material.pbrData.metallicRoughnessTexture
                                                  : nullptr,
        material.normalTexture ? &*material.normalTexture : nullptr,
        material.occlusionTexture ? &*material.occlusionTexture : nullptr,
        material.emissiveTexture ? &*material.emissiveTexture : nullptr};
    for (const auto* texture : textures) {
      if (texture == nullptr)
        continue;
      const auto index = texture->textureIndex;
      if (index >= asset.textures.size() || !asset.textures[index].imageIndex ||
          *asset.textures[index].imageIndex >= asset.images.size())
        return "材质纹理引用无效";
      if (texture->texCoordIndex > 1U)
        return "仅支持 UV0/UV1 纹理坐标";
      if (image_mime_type(asset.images[*asset.textures[index].imageIndex]) !=
          fastgltf::MimeType::PNG) {
        return "当前 PBR 纹理输入只支持 PNG，请显式转换其他编码";
      }
    }
  }
  for (const auto& image : asset.images) {
    if (image_mime_type(image) != fastgltf::MimeType::PNG) {
      return "当前图像导入只支持 PNG（包括未引用图片），请显式转换其他编码";
    }
  }
  for (const auto& mesh : asset.meshes) {
    for (const auto& primitive : mesh.primitives) {
      ++primitive_count;
      if (primitive.type != fastgltf::PrimitiveType::Triangles || !primitive.targets.empty() ||
          primitive.dracoCompression != nullptr) {
        return "首版只支持未压缩的静态三角形 Primitive";
      }
      if (primitive.findAttribute("POSITION") == primitive.attributes.end() ||
          primitive.findAttribute("NORMAL") == primitive.attributes.end() ||
          primitive.findAttribute("TEXCOORD_0") == primitive.attributes.end()) {
        return "Primitive 必须包含 POSITION、NORMAL 和 TEXCOORD_0";
      }
    }
  }
  return std::nullopt;
}

void populate_material_state(const fastgltf::Asset& asset, const fastgltf::Material& source,
                             import_ir_material& target) {
  target.alpha_mode =
      source.alphaMode == fastgltf::AlphaMode::Blend
          ? GNEISS_MATERIAL_ALPHA_BLEND
          : (source.alphaMode == fastgltf::AlphaMode::Mask ? GNEISS_MATERIAL_ALPHA_MASK
                                                           : GNEISS_MATERIAL_ALPHA_OPAQUE);
  target.double_sided = source.doubleSided ? 1U : 0U;
  target.alpha_cutoff = source.alphaCutoff;
  const std::array<const fastgltf::TextureInfo*, 5> textures{
      source.pbrData.baseColorTexture ? &*source.pbrData.baseColorTexture : nullptr,
      source.pbrData.metallicRoughnessTexture ? &*source.pbrData.metallicRoughnessTexture : nullptr,
      source.normalTexture ? &*source.normalTexture : nullptr,
      source.occlusionTexture ? &*source.occlusionTexture : nullptr,
      source.emissiveTexture ? &*source.emissiveTexture : nullptr};
  const auto address = [](fastgltf::Wrap wrap) {
    return wrap == fastgltf::Wrap::ClampToEdge
               ? GNEISS_TEXTURE_ADDRESS_CLAMP
               : (wrap == fastgltf::Wrap::MirroredRepeat ? GNEISS_TEXTURE_ADDRESS_MIRROR
                                                         : GNEISS_TEXTURE_ADDRESS_REPEAT);
  };
  for (std::size_t slot = 0; slot < textures.size(); ++slot) {
    const auto* texture = textures[slot];
    if (texture == nullptr)
      continue;
    auto& sampling = target.sampling[slot];
    sampling.uv_set = static_cast<std::uint32_t>(texture->texCoordIndex);
    const auto sampler_index = asset.textures[texture->textureIndex].samplerIndex;
    if (!sampler_index)
      continue;
    const auto& sampler = asset.samplers[*sampler_index];
    sampling.address_u = address(sampler.wrapS);
    sampling.address_v = address(sampler.wrapT);
    sampling.mag_filter = sampler.magFilter == fastgltf::Filter::Nearest
                              ? GNEISS_TEXTURE_FILTER_NEAREST
                              : GNEISS_TEXTURE_FILTER_LINEAR;
    const auto filter = sampler.minFilter.value_or(fastgltf::Filter::LinearMipMapLinear);
    sampling.min_filter =
        (filter == fastgltf::Filter::Nearest || filter == fastgltf::Filter::NearestMipMapNearest ||
         filter == fastgltf::Filter::NearestMipMapLinear)
            ? GNEISS_TEXTURE_FILTER_NEAREST
            : GNEISS_TEXTURE_FILTER_LINEAR;
    sampling.mip_filter =
        (filter == fastgltf::Filter::Nearest || filter == fastgltf::Filter::Linear)
            ? GNEISS_TEXTURE_MIP_NONE
            : ((filter == fastgltf::Filter::NearestMipMapNearest ||
                filter == fastgltf::Filter::LinearMipMapNearest)
                   ? GNEISS_TEXTURE_MIP_NEAREST
                   : GNEISS_TEXTURE_MIP_LINEAR);
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
[[nodiscard]] import_ir build_import_ir(const fastgltf::Asset& asset) {
  import_ir result;
  result.nodes.reserve(asset.nodes.size());
  for (const auto& node : asset.nodes) {
    import_ir_node ir_node;
    ir_node.name.assign(node.name.begin(), node.name.end());
    if (node.meshIndex) {
      ir_node.mesh_index = *node.meshIndex;
    }
    ir_node.children.assign(node.children.begin(), node.children.end());
    const auto& transform = std::get<fastgltf::TRS>(node.transform);
    for (std::size_t index = 0; index < 3U; ++index) {
      ir_node.translation[index] = transform.translation[index];
      ir_node.scale[index] = transform.scale[index];
    }
    const auto inverse_rotation_length =
        1.0F / std::sqrt((transform.rotation[0] * transform.rotation[0]) +
                         (transform.rotation[1] * transform.rotation[1]) +
                         (transform.rotation[2] * transform.rotation[2]) +
                         (transform.rotation[3] * transform.rotation[3]));
    for (std::size_t index = 0; index < 4U; ++index) {
      ir_node.rotation[index] = transform.rotation[index] * inverse_rotation_length;
    }
    result.nodes.push_back(std::move(ir_node));
  }

  result.meshes.reserve(asset.meshes.size());
  for (const auto& mesh : asset.meshes) {
    import_ir_mesh ir_mesh;
    ir_mesh.name.assign(mesh.name.begin(), mesh.name.end());
    ir_mesh.primitives.reserve(mesh.primitives.size());
    for (const auto& primitive : mesh.primitives) {
      const auto* const position = primitive.findAttribute("POSITION");
      const auto* const normal = primitive.findAttribute("NORMAL");
      const auto* const texcoord = primitive.findAttribute("TEXCOORD_0");
      import_ir_primitive ir_primitive;
      ir_primitive.position_accessor = position->accessorIndex;
      ir_primitive.normal_accessor = normal->accessorIndex;
      ir_primitive.texcoord_accessor = texcoord->accessorIndex;
      if (primitive.indicesAccessor) {
        ir_primitive.index_accessor = *primitive.indicesAccessor;
      }
      if (primitive.materialIndex) {
        ir_primitive.material_index = *primitive.materialIndex;
      }
      ir_mesh.primitives.push_back(ir_primitive);
    }
    result.meshes.push_back(std::move(ir_mesh));
  }

  result.materials.reserve(asset.materials.size());
  for (const auto& material : asset.materials) {
    import_ir_material ir_material;
    ir_material.name.assign(material.name.begin(), material.name.end());
    for (std::size_t index = 0; index < 4U; ++index) {
      ir_material.base_color[index] = material.pbrData.baseColorFactor[index];
    }
    ir_material.metallic = material.pbrData.metallicFactor;
    ir_material.roughness = material.pbrData.roughnessFactor;
    if (material.pbrData.baseColorTexture) {
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
      const auto texture_index = material.pbrData.baseColorTexture.value().textureIndex;
      if (texture_index < asset.textures.size() && asset.textures[texture_index].imageIndex) {
        ir_material.base_color_image_index = *asset.textures[texture_index].imageIndex;
      }
    }
    const auto image_index = [&](const auto& texture) -> std::optional<std::size_t> {
      if (!texture)
        return std::nullopt;
      return asset.textures[texture->textureIndex].imageIndex;
    };
    ir_material.metallic_roughness_image_index =
        image_index(material.pbrData.metallicRoughnessTexture);
    ir_material.normal_image_index = image_index(material.normalTexture);
    ir_material.occlusion_image_index = image_index(material.occlusionTexture);
    ir_material.emissive_image_index = image_index(material.emissiveTexture);
    if (material.normalTexture)
      ir_material.normal_scale = material.normalTexture->scale;
    if (material.occlusionTexture)
      ir_material.occlusion_strength = material.occlusionTexture->strength;
    for (std::size_t axis = 0; axis < 3U; ++axis)
      // 接受导出器在 1 附近的一 ULP 舍入误差，运行格式仍保持严格的 0..1。
      ir_material.emissive[axis] =
          material.emissiveFactor[axis] > 1.0F &&
                  material.emissiveFactor[axis] <= std::nextafter(1.0F, 2.0F)
              ? 1.0F
              : material.emissiveFactor[axis];
    populate_material_state(asset, material, ir_material);
    result.materials.push_back(std::move(ir_material));
  }

  result.images.reserve(asset.images.size());
  for (const auto& image : asset.images) {
    import_ir_image ir_image;
    ir_image.name.assign(image.name.begin(), image.name.end());
    ir_image.is_png = image_mime_type(image) == fastgltf::MimeType::PNG;
    copy_image_bytes(asset, image, ir_image);
    result.images.push_back(std::move(ir_image));
  }
  return result;
}

[[nodiscard]] bool is_float_vector(const fastgltf::Accessor& accessor,
                                   fastgltf::AccessorType type) {
  return accessor.type == type && accessor.componentType == fastgltf::ComponentType::Float &&
         !accessor.normalized;
}

[[nodiscard]] bool is_finite(const fastgltf::math::fvec3& value) {
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

[[nodiscard]] bool is_finite(const fastgltf::math::fvec2& value) {
  return std::isfinite(value[0]) && std::isfinite(value[1]);
}

[[nodiscard]] std::optional<std::string>
populate_optional_attributes(const fastgltf::Asset& asset, const fastgltf::Primitive& source,
                             import_ir_primitive& target) {
  const auto valid_component = [](const fastgltf::Accessor& a) {
    return (a.componentType == fastgltf::ComponentType::Float && !a.normalized) ||
           (a.normalized && (a.componentType == fastgltf::ComponentType::UnsignedByte ||
                             a.componentType == fastgltf::ComponentType::UnsignedShort));
  };
  const auto* uv = source.findAttribute("TEXCOORD_1");
  if (uv != source.attributes.end()) {
    const auto& accessor = asset.accessors[uv->accessorIndex];
    if (accessor.type != fastgltf::AccessorType::Vec2 || !valid_component(accessor) ||
        accessor.count != target.vertices.size())
      return "UV1 类型或数量无效";
    target.has_uv1 = true;
    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
        asset, accessor,
        [&](const auto& v, std::size_t i) { target.vertices[i].uv1 = {v[0], v[1]}; });
  }
  const auto* color = source.findAttribute("COLOR_0");
  if (color != source.attributes.end()) {
    const auto& accessor = asset.accessors[color->accessorIndex];
    if ((accessor.type != fastgltf::AccessorType::Vec3 &&
         accessor.type != fastgltf::AccessorType::Vec4) ||
        !valid_component(accessor) || accessor.count != target.vertices.size())
      return "顶点色类型或数量无效";
    target.has_color = true;
    if (accessor.type == fastgltf::AccessorType::Vec3)
      fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
          asset, accessor, [&](const auto& v, std::size_t i) {
            target.vertices[i].color = {v[0], v[1], v[2], 1.0F};
          });
    else
      fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
          asset, accessor, [&](const auto& v, std::size_t i) {
            target.vertices[i].color = {v[0], v[1], v[2], v[3]};
          });
  }
  if (source.materialIndex) {
    import_ir_material material;
    populate_material_state(asset, asset.materials[*source.materialIndex], material);
    for (const auto& sample : material.sampling)
      if (sample.uv_set == 1U && !target.has_uv1)
        return "材质引用 UV1，但网格缺少 TEXCOORD_1";
    target.tangent_uv_set = material.sampling[2].uv_set;
  }
  for (const auto& v : target.vertices) {
    if (!std::isfinite(v.uv1[0]) || !std::isfinite(v.uv1[1]) ||
        !std::ranges::all_of(v.color, [](float value) {
          return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
        }))
      return "UV1 或顶点色数值无效";
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> populate_primitive_data(const fastgltf::Asset& asset,
                                                                 const fastgltf::Primitive& source,
                                                                 import_ir_primitive& target) {
  const auto& positions = asset.accessors[target.position_accessor];
  const auto& normals = asset.accessors[target.normal_accessor];
  const auto& texcoords = asset.accessors[target.texcoord_accessor];
  if (!is_float_vector(positions, fastgltf::AccessorType::Vec3) ||
      !is_float_vector(normals, fastgltf::AccessorType::Vec3) ||
      !is_float_vector(texcoords, fastgltf::AccessorType::Vec2)) {
    return "POSITION/NORMAL/TEXCOORD_0 必须是未归一化的 Float Vec3/Vec3/Vec2";
  }
  if (positions.count != normals.count || positions.count != texcoords.count) {
    return "Primitive 顶点属性数量不一致";
  }

  target.vertices.resize(positions.count);
  fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
      asset, positions, [&](const auto& value, std::size_t index) {
        target.vertices[index].position[0] = value[0];
        target.vertices[index].position[1] = value[1];
        target.vertices[index].position[2] = value[2];
      });
  fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
      asset, normals, [&](const auto& value, std::size_t index) {
        target.vertices[index].normal[0] = value[0];
        target.vertices[index].normal[1] = value[1];
        target.vertices[index].normal[2] = value[2];
      });
  fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
      asset, texcoords, [&](const auto& value, std::size_t index) {
        target.vertices[index].texcoord[0] = value[0];
        target.vertices[index].texcoord[1] = value[1];
      });
  for (const auto& vertex : target.vertices) {
    if (!is_finite(
            fastgltf::math::fvec3{vertex.position[0], vertex.position[1], vertex.position[2]}) ||
        !is_finite(fastgltf::math::fvec3{vertex.normal[0], vertex.normal[1], vertex.normal[2]}) ||
        !is_finite(fastgltf::math::fvec2{vertex.texcoord[0], vertex.texcoord[1]})) {
      return "Primitive 顶点属性包含非有限数值";
    }
  }

  if (source.indicesAccessor) {
    const auto& indices = asset.accessors[*source.indicesAccessor];
    if (indices.type != fastgltf::AccessorType::Scalar ||
        (indices.componentType != fastgltf::ComponentType::UnsignedByte &&
         indices.componentType != fastgltf::ComponentType::UnsignedShort &&
         indices.componentType != fastgltf::ComponentType::UnsignedInt)) {
      return "索引必须是 UnsignedByte、UnsignedShort 或 UnsignedInt Scalar";
    }
    target.indices.reserve(indices.count);
    fastgltf::iterateAccessor<std::uint32_t>(
        asset, indices, [&](std::uint32_t value) { target.indices.push_back(value); });
  } else {
    target.indices.reserve(target.vertices.size());
    for (std::size_t index = 0; index < target.vertices.size(); ++index) {
      target.indices.push_back(static_cast<std::uint32_t>(index));
    }
  }
  for (const auto index : target.indices) {
    if (index >= target.vertices.size()) {
      return "Primitive 索引超出顶点范围";
    }
  }
  if (target.indices.size() % 3U != 0U) {
    return "三角形 Primitive 的索引数量必须是 3 的倍数";
  }
  if (auto error = populate_optional_attributes(asset, source, target))
    return error;
  const auto* tangent_attribute = source.findAttribute("TANGENT");
  if (tangent_attribute == source.attributes.end()) {
    return generate_tangents(target);
  }
  const auto& tangents = asset.accessors[tangent_attribute->accessorIndex];
  if (!is_float_vector(tangents, fastgltf::AccessorType::Vec4) ||
      tangents.count != positions.count) {
    return "TANGENT 必须是与顶点数量一致的 Float Vec4";
  }
  fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
      asset, tangents, [&](const auto& value, std::size_t index) {
        target.vertices[index].tangent = {value[0], value[1], value[2], value[3]};
      });
  for (const auto& vertex : target.vertices) {
    if (!std::ranges::all_of(vertex.tangent, [](float value) { return std::isfinite(value); }) ||
        (vertex.tangent[3] != 1.0F && vertex.tangent[3] != -1.0F))
      return "TANGENT 包含非有限值或无效手性";
  }
  for (auto& vertex : target.vertices) {
    auto& tangent = vertex.tangent;
    const auto& normal = vertex.normal;
    // 作者数据可能含舍入后的非正交切线；Gram-Schmidt 规范化但保留原始 UV 手性。
    const std::array<double, 3> n{normal[0], normal[1], normal[2]};
    std::array<double, 3> t{tangent[0], tangent[1], tangent[2]};
    const auto normal_length = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
    const auto projection = (n[0] * t[0] + n[1] * t[1] + n[2] * t[2]) / normal_length;
    for (std::size_t axis = 0; axis < 3U; ++axis)
      t[axis] -= projection * n[axis];
    const auto length = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    if (!std::isfinite(length) || (tangent[3] != 1.0F && tangent[3] != -1.0F))
      return "TANGENT 包含非有限值或无效手性";
    if (length < 1.0e-8) {
      target.repaired_tangents = true;
      return generate_tangents(target);
    }
    for (std::size_t axis = 0; axis < 3U; ++axis)
      tangent[axis] = static_cast<float>(t[axis] / length);
    if (!asset_internal::valid_mesh_tangent(tangent, {normal[0], normal[1], normal[2]})) {
      return "TANGENT 必须是正交单位切线且手性为正负一";
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> populate_vertex_data(const fastgltf::Asset& asset,
                                                              import_ir& data) {
  for (std::size_t mesh_index = 0; mesh_index < asset.meshes.size(); ++mesh_index) {
    const auto& source_mesh = asset.meshes[mesh_index];
    auto& target_mesh = data.meshes[mesh_index];
    for (std::size_t primitive_index = 0; primitive_index < source_mesh.primitives.size();
         ++primitive_index) {
      if (auto error = populate_primitive_data(asset, source_mesh.primitives[primitive_index],
                                               target_mesh.primitives[primitive_index])) {
        return error;
      }
    }
  }
  return std::nullopt;
}

} // namespace

inspect_report inspect_gltf(const std::filesystem::path& source_path) {
  if (source_path.empty()) {
    return failure(inspect_result::invalid_argument, "源文件路径不能为空");
  }

  try {
    auto source = fastgltf::GltfDataBuffer::FromPath(source_path);
    if (!source) {
      return failure(inspect_result::source_unavailable,
                     read_failure_detail(fastgltf::getErrorMessage(source.error())));
    }

    fastgltf::Parser preflight_parser;
    auto preflight = preflight_parser.loadGltf(source.get(), source_path.parent_path());
    if (preflight.error() != fastgltf::Error::None) {
      return failure(inspect_result::invalid_source,
                     parse_failure_detail(fastgltf::getErrorMessage(preflight.error())));
    }
    if (!asset_uris_are_safe(preflight.get(), source_path.parent_path())) {
      return failure(inspect_result::invalid_source, "外部资源 URI 不能逃逸源文件目录");
    }
    source.get().reset();

    fastgltf::Parser parser;
    constexpr auto options = fastgltf::Options::LoadExternalBuffers |
                             fastgltf::Options::LoadExternalImages |
                             fastgltf::Options::DecomposeNodeMatrices;
    auto loaded = parser.loadGltf(source.get(), source_path.parent_path(), options);
    if (loaded.error() != fastgltf::Error::None) {
      return failure(inspect_result::invalid_source,
                     parse_failure_detail(fastgltf::getErrorMessage(loaded.error())));
    }

    auto& asset = loaded.get();
    if (const auto validation = fastgltf::validate(asset); validation != fastgltf::Error::None) {
      return failure(inspect_result::invalid_source,
                     parse_failure_detail(fastgltf::getErrorMessage(validation)));
    }
    if (!accessors_fit_buffers(asset)) {
      return failure(inspect_result::invalid_source, "Accessor 或 BufferView 超出 Buffer 边界");
    }
    std::size_t primitive_count = 0;
    if (auto unsupported = validate_supported_scope(asset, primitive_count)) {
      return failure(inspect_result::unsupported_feature, std::move(*unsupported));
    }

    inspect_report report;
    report.result = inspect_result::success;
    report.summary.scene_count = asset.scenes.size();
    report.summary.node_count = asset.nodes.size();
    report.summary.mesh_count = asset.meshes.size();
    report.summary.primitive_count = primitive_count;
    report.summary.material_count = asset.materials.size();
    report.summary.image_count = asset.images.size();

    report.data = build_import_ir(asset);
    if (auto invalid_data = populate_vertex_data(asset, report.data)) {
      return failure(inspect_result::invalid_source, std::move(*invalid_data));
    }
    std::size_t repaired{};
    for (const auto& mesh : report.data.meshes)
      for (const auto& primitive : mesh.primitives)
        repaired += primitive.repaired_tangents ? 1U : 0U;
    if (repaired != 0U)
      report.diagnostic =
          "已用 MikkTSpace 重新生成退化作者切线，Primitive 数=" + std::to_string(repaired);
    for (const auto& material : asset.materials) {
      for (std::size_t axis = 0; axis < 3U; ++axis) {
        if (material.emissiveFactor[axis] > 1.0F &&
            material.emissiveFactor[axis] <= std::nextafter(1.0F, 2.0F))
          report.diagnostic += "；已将自发光因子的一 ULP 越界舍入为 1";
      }
    }
    return report;
  } catch (const std::exception& error) {
    return failure(inspect_result::invalid_source,
                   std::string{"glTF 检查发生异常："} + error.what());
  } catch (...) {
    return failure(inspect_result::invalid_source, "glTF 检查发生未知异常");
  }
}

} // namespace gneiss::tooling::asset_import
