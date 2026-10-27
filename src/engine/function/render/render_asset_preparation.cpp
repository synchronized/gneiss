// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render_asset_preparation.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/mesh/mesh_binary.hpp"
#include "engine/resource/mesh/mesh_decoder.hpp"
#include "engine/resource/texture/texture_decoder.hpp"
#include <algorithm>
#include <cmath>
namespace gneiss::engine {
using namespace gneiss::engine::detail;

namespace {
bool invalid_prepared_mesh(const mesh_data& mesh) {
  return (mesh.vertices.size() < 3U ||
          (!mesh.normals.empty() && mesh.normals.size() != mesh.vertices.size()) ||
          (!mesh.indices.empty() && (mesh.indices.size() < 3U || mesh.indices.size() % 3U != 0U)) ||
          !std::ranges::all_of(mesh.vertices,
                               [](const auto& v) {
                                 return std::isfinite(v.x) && std::isfinite(v.y) &&
                                        std::isfinite(v.z) && std::isfinite(v.u) &&
                                        std::isfinite(v.v);
                               }) ||
          !std::ranges::all_of(mesh.normals,
                               [](const auto& n) {
                                 const auto length =
                                     std::sqrt((n.x * n.x) + (n.y * n.y) + (n.z * n.z));
                                 return std::isfinite(length) && std::abs(length - 1.0F) <= 1.0e-4F;
                               }) ||
          !std::ranges::all_of(mesh.indices, [&](auto i) { return i < mesh.vertices.size(); }));
}
gneiss::engine::result decode_non_texture(const asset_preparation_source& source,
                                          const std::vector<std::byte>& bytes,
                                          prepared_asset& asset, asset_diagnostic& diagnostic,
                                          std::size_t material_bytes, std::size_t maximum_bytes) {
  auto result = gneiss::engine::result::success;
  if (source.type == asset_type::mesh) {
    result =
        gneiss::engine::is_mesh_binary(bytes)
            ? parse_binary_mesh(bytes, asset.mesh.vertices, asset.mesh.normals, asset.mesh.indices,
                                asset.mesh.tangents, asset.mesh.uv1, asset.mesh.colors, diagnostic)
            : parse_mesh(bytes, asset.mesh.vertices, asset.mesh.normals, diagnostic);
    // 此校验与 RID 创建的顶点/法线/索引契约一致，在后台完成。
    const auto& mesh = asset.mesh;
    if (result == gneiss::engine::result::success && invalid_prepared_mesh(mesh)) {
      result = gneiss::engine::result::invalid_argument;
    }
    asset.bytes = mesh.data_bytes();
  } else if (source.type == asset_type::material) {
    if (!asset.description)
      return gneiss::engine::result::unsupported;
    asset.material = asset.description->parameters;
    asset.texture_uris = asset.description->texture_uris;
    // 由调用方传入发布预算，Asset 不依赖资源对象布局。
    if (material_bytes == 0U) {
      fail(diagnostic, gneiss::engine::result::invalid_argument, source.uri,
           "材质发布预算不得为零");
      return gneiss::engine::result::invalid_argument;
    }
    if (material_bytes > maximum_bytes) {
      fail(diagnostic, gneiss::engine::result::out_of_memory, source.uri,
           "材质发布预算超过批次上限");
      return gneiss::engine::result::out_of_memory;
    }
    asset.bytes = material_bytes;
    for (const auto& uri : asset.texture_uris) {
      if (uri.size() > maximum_bytes - asset.bytes) {
        fail(diagnostic, gneiss::engine::result::out_of_memory, source.uri,
             "材质依赖超出批次字节上限");
        return gneiss::engine::result::out_of_memory;
      }
      asset.bytes += uri.size();
    }
  } else {
    result = gneiss::engine::result::invalid_argument;
  }
  return result;
}
} // namespace
result decode_loaded_asset(const asset_preparation_source& source,
                           std::vector<std::byte> description, std::string payload_uri,
                           std::vector<std::byte> payload, prepared_asset& output,
                           asset_diagnostic& diagnostic, std::size_t maximum_bytes,
                           texture_prepare_profile profile,
                           asset_ref<const material_description> material,
                           std::size_t material_bytes) {
  prepared_asset decoded;
  decoded.source = source;
  decoded.description = std::move(material);
  result status;
  if (source.type == asset_type::texture) {
    if (description.size() > maximum_bytes || payload.size() > maximum_bytes)
      return result::invalid_argument;
    texture_description texture;
    status = parse_texture(description, texture, diagnostic);
    if (status.failed())
      return status;
    if (texture.uri != payload_uri) {
      fail(diagnostic, result::invalid_argument, "/source", "纹理负载 URI 与描述不匹配");
      return result::invalid_argument;
    }
    status = decode_texture_payload(texture, payload, decoded.texture, diagnostic, maximum_bytes,
                                    maximum_bytes, profile);
    decoded.bytes = decoded.texture.manifest.size() + decoded.texture.payload.size();
    for (const auto& level : decoded.texture.levels)
      decoded.bytes += level.pixels.size();
  } else {
    status =
        decode_non_texture(source, description, decoded, diagnostic, material_bytes, maximum_bytes);
  }
  if (status.failed())
    return status;
  if (decoded.bytes > maximum_bytes)
    return result::out_of_memory;
  output = std::move(decoded);
  return result::success;
}

} // namespace gneiss::engine
