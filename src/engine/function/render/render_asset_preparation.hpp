// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/result.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/resource/loaders/asset_preparation.hpp"

namespace gneiss::engine {
using render_asset_type = gneiss::engine::asset_type;
using render_asset_reload = gneiss::engine::asset_preparation_source;
using prepared_render_asset = gneiss::engine::prepared_asset;
using prepared_render_batch = gneiss::engine::prepared_batch;

} // namespace gneiss::engine
