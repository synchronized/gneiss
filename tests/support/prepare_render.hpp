// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_asset.hpp"
#include "engine/function/render/render_asset_preparation.hpp"
#include "engine/resource/loaders/asset_parsing.hpp"
#include "engine/resource/types/material_description.hpp"
#include <algorithm>
#include <set>
namespace gneiss::test_support {
using namespace gneiss::engine;
// 仅组织内存测试夹具；生产解码器只消费字节，不负责读取与递归依赖。
inline result decode_fixture(const virtual_file_system& files,
                             const asset_preparation_source& source, prepared_asset& output,
                             asset_diagnostic& diagnostic, std::size_t input_limit,
                             std::size_t output_limit, texture_prepare_profile profile = {}) {
  std::vector<std::byte> description, payload;
  auto status = files.read_bounded(source.uri, input_limit, description);
  if (status.failed())
    return status;
  std::string payload_uri;
  if (source.type == asset_type::texture) {
    texture_description parsed;
    status = detail::parse_texture(description, parsed, diagnostic);
    if (status.failed())
      return status;
    payload_uri = parsed.uri;
    status = files.read_bounded(payload_uri, input_limit, payload);
    if (status.failed())
      return status;
  }
  const auto description_digest = sha256(description);
  const auto payload_digest = sha256(payload);
  const auto description_size = description.size(), payload_size = payload.size();
  status = decode_loaded_asset(source, std::move(description), payload_uri, std::move(payload),
                               output, diagnostic, std::max(input_limit, output_limit), profile);
  if (status.failed())
    return status;
  if (output.bytes > output_limit) {
    output = {};
    diagnostic = {result::out_of_memory, 0, "/source", "测试输出超过准备预算"};
    return result::out_of_memory;
  }
  asset_identity identity;
  status = files.identify(source.uri, static_cast<std::uint32_t>(source.type), identity);
  if (status.failed())
    return status;
  output.sources.push_back({identity, description_size, description_digest});
  if (!payload_uri.empty()) {
    status = files.identify(payload_uri, static_cast<std::uint32_t>(source.type), identity);
    if (status.failed())
      return status;
    output.sources.push_back({identity, payload_size, payload_digest});
  }
  return result::success;
}
inline result decode_texture_fixture(const virtual_file_system& files, std::string_view uri,
                                     prepared_texture_data& output, asset_diagnostic& diagnostic,
                                     std::size_t input_limit,
                                     std::size_t output_limit = 256U * 1024U * 1024U,
                                     texture_prepare_profile profile = {}) {
  output = {};
  diagnostic = {};
  prepared_asset decoded;
  const auto status = decode_fixture(files, {std::string(uri), asset_type::texture}, decoded,
                                     diagnostic, input_limit, output_limit, profile);
  if (status.ok())
    output = std::move(decoded.texture);
  return status;
}
inline result prepare_render_for_test(asset_manager& assets, const virtual_file_system& files,
                                      std::span<const render_asset_reload> requested,
                                      prepared_render_batch& output, asset_diagnostic& diagnostic,
                                      const std::function<bool()>& cancelled,
                                      std::size_t maximum_assets = 256U,
                                      std::size_t maximum_bytes = 256U * 1024U * 1024U,
                                      texture_prepare_profile profile = {}, bool reload = false) {
  output = {};
  diagnostic = {};
  if (requested.empty() || requested.size() > maximum_assets)
    return result::invalid_argument;
  prepared_batch batch;
  std::vector<asset_preparation_source> pending(requested.begin(), requested.end());
  std::set<std::pair<std::string, asset_type>> seen;
  for (std::size_t index = 0; index < pending.size(); ++index) {
    if (cancelled && cancelled())
      return result::invalid_state;
    const auto source = pending[index];
    if (!seen.emplace(source.uri, source.type).second)
      continue;
    if (seen.size() > maximum_assets)
      return result::out_of_memory;
    prepared_asset decoded;
    result status;
    if (source.type == asset_type::material) {
      asset_ref<const material_description> material;
      meta::serialization_diagnostic error;
      status = load_asset(assets, source.uri, material, reload, &error);
      if (status.failed()) {
        diagnostic = {status, error.byte_offset, std::move(error.path), std::move(error.message)};
        return status;
      }
      status = decode_loaded_asset(source, {}, {}, {}, decoded, diagnostic, maximum_bytes, profile,
                                   std::move(material), sizeof(material_data));
    } else {
      status = decode_fixture(files, source, decoded, diagnostic, 64U * 1024U * 1024U,
                              maximum_bytes, profile);
    }
    if (status.failed())
      return status;
    if (cancelled && cancelled())
      return result::invalid_state;
    if (decoded.bytes > maximum_bytes - batch.bytes)
      return result::out_of_memory;
    batch.bytes += decoded.bytes;
    for (const auto& uri : decoded.texture_uris)
      if (!uri.empty())
        pending.push_back({uri, asset_type::texture});
    batch.assets.push_back(std::move(decoded));
  }
  std::stable_sort(batch.assets.begin(), batch.assets.end(), [](const auto& a, const auto& b) {
    const auto order = [](asset_type type) {
      return type == asset_type::texture ? 0 : type == asset_type::mesh ? 1 : 2;
    };
    return order(a.source.type) < order(b.source.type);
  });
  output = std::move(batch);
  return result::success;
}
}
