// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/detail/render_asset_loader.hpp"
#include "engine/function/resource/detail/texture_load_service.hpp"
namespace gneiss::engine {
struct asset_manager::render_state {
  render_resource_factory factory;
  render_asset_loader loader;
  std::unique_ptr<texture_load_service> batch;
  explicit render_state(asset_manager& assets, render_service& render)
      : factory(render), loader(assets, factory) {}
};
}
