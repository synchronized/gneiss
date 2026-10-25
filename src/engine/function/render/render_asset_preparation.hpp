// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/result.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/resource/loaders/asset_preparation.hpp"

namespace gneiss::engine {
class asset_manager;

using render_asset_type = gneiss::engine::asset_type;
using render_asset_reload = gneiss::engine::asset_preparation_source;
using prepared_render_asset = gneiss::engine::prepared_asset;
using prepared_render_batch = gneiss::engine::prepared_batch;
using gneiss::engine::prepare_texture;

/** 所属线程同步准备：材质经 manager 加载，再交给纯 CPU 准备器展开依赖。
 * reload 强制更新材质描述；不创建 RID，网格和纹理保留专用读取流程。 */
[[nodiscard]] GNEISS_API gneiss::engine::result
prepare_render_assets(asset_manager& assets, std::span<const render_asset_reload> requested,
                      prepared_render_batch& output, asset_diagnostic& diagnostic,
                      const std::function<bool()>& cancelled, std::size_t maximum_assets = 256U,
                      std::size_t maximum_bytes = 256U * 1024U * 1024U,
                      texture_prepare_profile profile = {}, bool reload = false) noexcept;

} // namespace gneiss::engine
