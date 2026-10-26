// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_asset.hpp"
#include "engine/function/render/render_asset_preparation.hpp"
#include "engine/resource/types/material_description.hpp"
namespace gneiss::test_support {
using namespace gneiss::engine;
// 迁移中的准备器测试夹具；材质始终经异步资源入口取得。
inline gneiss::engine::result
prepare_render_for_test(asset_manager& assets, const virtual_file_system& files,
                        std::span<const render_asset_reload> requested,
                        prepared_render_batch& output, asset_diagnostic& diagnostic,
                        const std::function<bool()>& cancelled, std::size_t maximum_assets = 256U,
                        std::size_t maximum_bytes = 256U * 1024U * 1024U,
                        texture_prepare_profile profile = {}, bool reload = false) noexcept try {
  output = {};
  diagnostic = {};
  if (requested.empty() || requested.size() > maximum_assets)
    return result::invalid_argument;
  material_descriptions materials;
  for (const auto& source : requested) {
    if (cancelled && cancelled())
      return result::invalid_state;
    if (source.type != asset_type::material || materials.contains(source.uri))
      continue;
    asset_ref<const material_description> description;
    gneiss::engine::meta::serialization_diagnostic decoded;
    const auto status = load_asset(assets, source.uri, description, reload, &decoded);
    if (status.failed()) {
      diagnostic = {status, decoded.byte_offset, std::move(decoded.path),
                    std::move(decoded.message)};
      return status;
    }
    materials.emplace(source.uri, std::move(description));
  }
  return gneiss::engine::prepare_assets(files, requested, output, diagnostic, cancelled,
                                        sizeof(material_data), maximum_assets, maximum_bytes,
                                        profile, std::move(materials));
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}

}
