// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/asset/asset_preparation.hpp"
#include "engine/function/render/render_resource_data.hpp"

namespace gneiss::render_internal {

using asset_diagnostic = asset_internal::asset_diagnostic;
using render_asset_type = asset_internal::asset_type;
using render_asset_reload = asset_internal::asset_request;
using prepared_render_asset = asset_internal::prepared_asset;
using prepared_render_batch = asset_internal::prepared_batch;
using asset_internal::prepare_texture;

/** Render 指定发布资源预算；CPU 准备唯一实现位于 Asset。 */
[[nodiscard]] inline gneiss_result
prepare_render_assets(const asset_internal::virtual_file_system& file_system,
                      std::span<const render_asset_reload> requested, prepared_render_batch& output,
                      asset_diagnostic& diagnostic, const std::function<bool()>& cancelled,
                      std::size_t maximum_assets = 256U,
                      std::size_t maximum_bytes = 256U * 1024U * 1024U,
                      texture_prepare_profile profile = {}) noexcept {
  return asset_internal::prepare_assets(file_system, requested, output, diagnostic, cancelled,
                                        sizeof(material_resource), maximum_assets, maximum_bytes,
                                        profile);
}

} // namespace gneiss::render_internal
