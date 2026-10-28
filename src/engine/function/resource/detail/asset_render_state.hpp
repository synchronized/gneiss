// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/resource/detail/asset_decode.hpp"
#include "engine/function/resource/detail/asset_load_batch.hpp"
#include "engine/function/resource/detail/render_resource_factory.hpp"
#include "engine/function/resource/resource_service.hpp"
namespace gneiss::engine {
namespace detail {
/** 单项 CPU 内容采用任务；不是资源版本或批次事务。 */
struct GNEISS_API asset_update {
  struct material_content {
    std::shared_ptr<const material_data> data;
    std::array<texture, 5> dependencies;
  };
  using content = std::variant<std::monostate, std::shared_ptr<const mesh_data>,
                               std::shared_ptr<const texture_data>, material_content>;
  render_asset_ref target;
  std::uint64_t sequence{};
  content data;
  static std::uint64_t next_update_sequence() noexcept;
  static bool claim_update(resource_service&, const render_asset_ref&, std::uint64_t) noexcept;
  static result prepare_update(resource_service&, decoded_asset, asset_update&,
                               const render_asset_ref& = {}, std::uint64_t = 0U) noexcept;
  static result apply_update(resource_service&, asset_update&) noexcept;

private:
  static std::uint64_t update_sequence(resource_service&, const render_asset_ref&) noexcept;
  static result prepare_material_update(resource_service&, const material_description&,
                                        asset_update&);
};
}
struct resource_service::render_state {
  render_resource_factory factory;
  texture_prepare_profile default_profile{};
  std::unique_ptr<asset_load_batch> batch;
  explicit render_state(render_service& render) : factory(render) {}
};
} // namespace gneiss::engine
