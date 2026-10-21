// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/function/render/render_resource_data.hpp"
#include "engine/resource/loaders/asset_preparation.hpp"

namespace gneiss::render_internal {

using asset_diagnostic = engine::resource::asset_diagnostic;
using render_asset_type = engine::resource::asset_type;
using render_asset_reload = engine::resource::asset_request;
using prepared_render_asset = engine::resource::prepared_asset;
using prepared_render_batch = engine::resource::prepared_batch;
using engine::resource::prepare_texture;

/** Render 指定发布资源预算；CPU 准备唯一实现位于 Asset。 */
[[nodiscard]] inline gneiss_result
prepare_render_assets(const engine::resource::virtual_file_system& file_system,
                      std::span<const render_asset_reload> requested, prepared_render_batch& output,
                      asset_diagnostic& diagnostic, const std::function<bool()>& cancelled,
                      std::size_t maximum_assets = 256U,
                      std::size_t maximum_bytes = 256U * 1024U * 1024U,
                      texture_prepare_profile profile = {}) noexcept {
  return engine::resource::prepare_assets(file_system, requested, output, diagnostic, cancelled,
                                          sizeof(material_resource), maximum_assets, maximum_bytes,
                                          profile);
}

} // namespace gneiss::render_internal
