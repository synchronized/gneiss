// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_asset.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/function/resource/detail/asset_decode.hpp"
#include "engine/function/resource/detail/resource_service_impl.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/texture/texture_decoder.hpp"
#include "value_result.hpp"
#include <algorithm>
#include <set>
namespace gneiss::engine {
struct prepared_batch {
  std::vector<decoded_asset> assets;
  std::size_t bytes{};
  /** 整文件快照持有的输入字节；区间读取没有源副本，不计入此值。 */
  std::size_t input_bytes{};
};
}
namespace gneiss::test_support {
using namespace gneiss::engine;
inline error adopt_render_for_test(resource_service& service, decoded_asset decoded,
                                   render_resource_ref& target) {
  const auto kind = decoded.kind();
  if (kind == render_resource_kind::invalid)
    return error::invalid_argument;
  if (!target) {
    const auto status =
        gneiss::test_support::take_value(service.reserve_resource({decoded.uri, kind}), target);
    if (status.failed())
      return status;
  }
  if (resource_kind(target) != kind || resource_uri(target) != decoded.uri)
    return error::invalid_argument;
  return std::visit(
      [&](auto& data) -> error {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, mesh_data>)
          return resource_service::implementation::adopt(
              service, std::make_shared<const mesh_data>(std::move(data)),
              resource_cast<mesh_resource>(target), decoded.sources);
        else if constexpr (std::is_same_v<T, texture_data>)
          return resource_service::implementation::adopt(
              service, std::make_shared<const texture_data>(std::move(data)),
              resource_cast<texture_resource>(target), decoded.sources);
        else if constexpr (std::is_same_v<T, asset_ref<const material_description>>) {
          if (!data)
            return error::invalid_argument;
          std::array<texture, 5> dependencies;
          for (std::size_t slot = 0; slot < dependencies.size(); ++slot) {
            if (data->texture_uris[slot].empty())
              continue;
            render_resource_ref entry;
            const auto status = gneiss::test_support::take_value(
                service.reserve_resource({data->texture_uris[slot], render_resource_kind::texture}),
                entry);
            if (status.failed())
              return status;
            dependencies[slot] = resource_cast<texture_resource>(entry);
          }
          return resource_service::implementation::adopt(
              service, *data, resource_cast<material_resource>(target), std::move(dependencies));
        } else
          return error::invalid_argument;
      },
      decoded.content);
}
// 仅组织内存测试夹具；生产解码器只消费字节，不负责读取与递归依赖。
inline error decode_fixture(const gneiss::test_support::fixture_files& files,
                            const render_resource_source& source, decoded_asset& output,
                            asset_diagnostic& diagnostic, std::size_t input_limit,
                            std::size_t output_limit, texture_prepare_profile profile = {}) {
  std::vector<std::byte> description, payload;
  auto status = files.read_bounded(source.uri, input_limit, description);
  if (status.failed())
    return status;
  std::string payload_uri;
  texture_description parsed;
  if (source.type == render_resource_kind::texture) {
    status = assign_decoded(detail::parse_texture(description), parsed, diagnostic);
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
  if (source.type == render_resource_kind::texture)
    status = gneiss::test_support::assign_decoded(
        decode_texture_asset(source.uri, parsed, payload, profile), output, diagnostic);
  else if (source.type == render_resource_kind::mesh)
    status = gneiss::test_support::assign_decoded(decode_mesh_asset(source.uri, description),
                                                  output, diagnostic);
  else
    return error::invalid_argument;
  if (status.failed())
    return status;
  if (output.bytes > output_limit) {
    output = {};
    diagnostic = {error::out_of_memory, 0, "/source", "测试输出超过准备预算"};
    return error::out_of_memory;
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
  return error::success;
}
inline error decode_texture_fixture(const gneiss::test_support::fixture_files& files,
                                    std::string_view uri, texture_data& output,
                                    asset_diagnostic& diagnostic, std::size_t input_limit,
                                    std::size_t output_limit = 256U * 1024U * 1024U,
                                    texture_prepare_profile profile = {}) {
  output = {};
  diagnostic = {};
  decoded_asset decoded;
  const auto status = decode_fixture(files, {std::string(uri), render_resource_kind::texture},
                                     decoded, diagnostic, input_limit, output_limit, profile);
  if (status.ok())
    output = std::move(std::get<texture_data>(decoded.content));
  return status;
}
inline error prepare_render_for_test(asset_manager& assets,
                                     const gneiss::test_support::fixture_files& files,
                                     std::span<const render_resource_source> requested,
                                     prepared_batch& output, asset_diagnostic& diagnostic,
                                     const std::function<bool()>& cancelled,
                                     std::size_t maximum_assets = 256U,
                                     std::size_t maximum_bytes = 256U * 1024U * 1024U,
                                     texture_prepare_profile profile = {}, bool reload = false) {
  output = {};
  diagnostic = {};
  if (requested.empty() || requested.size() > maximum_assets)
    return error::invalid_argument;
  prepared_batch batch;
  std::vector<render_resource_source> pending(requested.begin(), requested.end());
  std::set<std::pair<std::string, render_resource_kind>> seen;
  for (std::size_t index = 0; index < pending.size(); ++index) {
    if (cancelled && cancelled())
      return error::invalid_state;
    const auto source = pending[index];
    if (!seen.emplace(source.uri, source.type).second)
      continue;
    if (seen.size() > maximum_assets)
      return error::out_of_memory;
    decoded_asset decoded;
    error status;
    if (source.type == render_resource_kind::material) {
      asset_ref<const material_description> material;
      meta::serialization_diagnostic error;
      status = load_asset(assets, source.uri, material, reload, &error);
      if (status.failed()) {
        diagnostic = {status, error.byte_offset, std::move(error.path), std::move(error.message)};
        return status;
      }
      status = gneiss::test_support::assign_decoded(
          adopt_material_asset(source.uri, std::move(material), sizeof(material_data)), decoded,
          diagnostic);
    } else {
      status = decode_fixture(files, source, decoded, diagnostic, 64U * 1024U * 1024U,
                              maximum_bytes, profile);
    }
    if (status.failed())
      return status;
    if (cancelled && cancelled())
      return error::invalid_state;
    if (decoded.bytes > maximum_bytes - batch.bytes)
      return error::out_of_memory;
    batch.bytes += decoded.bytes;
    if (const auto* material = std::get_if<asset_ref<const material_description>>(&decoded.content))
      for (const auto& uri : (*material)->texture_uris)
        if (!uri.empty())
          pending.push_back({uri, render_resource_kind::texture});
    batch.assets.push_back(std::move(decoded));
  }
  std::stable_sort(batch.assets.begin(), batch.assets.end(), [](const auto& a, const auto& b) {
    const auto order = [](render_resource_kind type) {
      return type == render_resource_kind::texture ? 0 : type == render_resource_kind::mesh ? 1 : 2;
    };
    return order(a.kind()) < order(b.kind());
  });
  output = std::move(batch);
  return error::success;
}
}
