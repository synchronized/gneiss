// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/detail/asset_decode.hpp"
#include "engine/function/resource/detail/asset_load_batch.hpp"
#include "engine/function/resource/detail/render_resource_factory.hpp"
namespace gneiss::engine {
namespace detail {
/** 单项 CPU 内容采用任务；不是资源版本或批次事务。 */
struct GNEISS_API asset_update {
  struct content {
    std::shared_ptr<const mesh_data> mesh;
    std::shared_ptr<const material_data> material;
    std::shared_ptr<const texture_data> texture;
    std::size_t bytes{};
  };
  render_asset_ref target;
  std::uint64_t sequence{};
  content data;
  std::array<texture, 5> dependencies;
  static std::uint64_t next_update_sequence() noexcept;
  static bool claim_update(asset_manager&, const render_asset_ref&, std::uint64_t) noexcept;
  static result prepare_update(asset_manager&, prepared_asset, asset_update&,
                               const render_asset_ref& = {}, std::uint64_t = 0U) noexcept;
  static result apply_update(asset_manager&, asset_update&) noexcept;

private:
  static std::uint64_t update_sequence(asset_manager&, const render_asset_ref&) noexcept;
  static result prepare_material_update(asset_manager&, prepared_asset&, asset_update&);
};
}
struct asset_manager::render_state {
  render_resource_factory factory;
  texture_prepare_profile default_profile{};
  std::unique_ptr<asset_load_batch> batch;
  explicit render_state(render_service& render) : factory(render) {}
};
} // namespace gneiss::engine
