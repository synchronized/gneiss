// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_decode.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/mesh/mesh_binary.hpp"
#include "engine/resource/mesh/mesh_decoder.hpp"
#include "engine/resource/texture/texture_decoder.hpp"
#include <algorithm>
#include <cmath>
namespace gneiss::engine {
using namespace engine::detail;

namespace {
bool invalid_prepared_mesh(const mesh_data& mesh) {
  return (mesh.vertices.size() < 3U ||
          (!mesh.normals.empty() && mesh.normals.size() != mesh.vertices.size()) ||
          (!mesh.indices.empty() && (mesh.indices.size() < 3U || mesh.indices.size() % 3U != 0U)) ||
          !std::ranges::all_of(mesh.vertices,
                               [](const auto& v) {
                                 return std::isfinite(v.position.x) &&
                                        std::isfinite(v.position.y) &&
                                        std::isfinite(v.position.z) && std::isfinite(v.uv.x) &&
                                        std::isfinite(v.uv.y);
                               }) ||
          !std::ranges::all_of(mesh.normals,
                               [](const auto& n) {
                                 const auto length =
                                     std::sqrt((n.x * n.x) + (n.y * n.y) + (n.z * n.z));
                                 return std::isfinite(length) && std::abs(length - 1.0F) <= 1.0e-4F;
                               }) ||
          !std::ranges::all_of(mesh.indices, [&](auto i) { return i < mesh.vertices.size(); }));
}
} // namespace
result<decoded_asset, asset_diagnostic> decode_mesh_asset(std::string_view uri,
                                                          const std::vector<std::byte>& bytes,
                                                          std::size_t maximum_bytes) {
  asset_diagnostic diagnostic;
  const auto failure = [&](error code) {
    diagnostic.result = code;
    if (diagnostic.path.empty())
      diagnostic.path = uri;
    return std::move(diagnostic);
  };
  mesh_data mesh;
  auto status = is_mesh_binary(bytes)
                    ? parse_binary_mesh(bytes, mesh.vertices, mesh.normals, mesh.indices,
                                        mesh.tangents, mesh.uv1, mesh.colors, diagnostic)
                    : parse_mesh(bytes, mesh.vertices, mesh.normals, diagnostic);
  if (status.failed())
    return failure(status);
  if (invalid_prepared_mesh(mesh))
    return failure(error::invalid_argument);
  const auto size = mesh.data_bytes();
  if (size > maximum_bytes)
    return failure(error::out_of_memory);
  return decoded_asset{
      .uri = std::string(uri), .sources = {}, .content = std::move(mesh), .bytes = size};
}
result<decoded_asset, asset_diagnostic> decode_texture_asset(std::string_view uri,
                                                             const texture_description& description,
                                                             const std::vector<std::byte>& payload,
                                                             std::size_t maximum_bytes,
                                                             texture_prepare_profile profile) {
  asset_diagnostic diagnostic;
  const auto failure = [&](error code) {
    diagnostic.result = code;
    if (diagnostic.path.empty())
      diagnostic.path = uri;
    return std::move(diagnostic);
  };
  if (payload.size() > maximum_bytes)
    return failure(error::invalid_argument);
  texture_data texture;
  const auto status = decode_texture_payload(description, payload, texture, diagnostic,
                                             maximum_bytes, maximum_bytes, profile);
  if (status.failed())
    return failure(status);
  auto size = texture.manifest.size() + texture.payload.size();
  for (const auto& level : texture.levels)
    size += level.pixels.size();
  if (size > maximum_bytes)
    return failure(error::out_of_memory);
  return decoded_asset{
      .uri = std::string(uri), .sources = {}, .content = std::move(texture), .bytes = size};
}
result<decoded_asset, asset_diagnostic>
adopt_material_asset(std::string_view uri, asset_ref<const material_description> material,
                     std::size_t maximum_bytes, std::size_t material_bytes) {
  asset_diagnostic diagnostic;
  const auto failure = [&](error code) {
    diagnostic.result = code;
    if (diagnostic.path.empty())
      diagnostic.path = uri;
    return std::move(diagnostic);
  };
  if (!material)
    return failure(error::invalid_argument);
  if (material_bytes == 0U) {
    fail(diagnostic, error::invalid_argument, uri, "材质内容预算不得为零");
    return failure(error::invalid_argument);
  }
  if (material_bytes > maximum_bytes)
    return failure(error::out_of_memory);
  auto size = material_bytes;
  for (const auto& dependency : material->texture_uris) {
    if (dependency.size() > maximum_bytes - size) {
      fail(diagnostic, error::out_of_memory, uri, "材质依赖超出单资源字节上限");
      return failure(error::out_of_memory);
    }
    size += dependency.size();
  }
  return decoded_asset{
      .uri = std::string(uri), .sources = {}, .content = std::move(material), .bytes = size};
}
} // namespace gneiss::engine
