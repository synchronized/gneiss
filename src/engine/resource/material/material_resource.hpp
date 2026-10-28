// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/rid.hpp"
#include "engine/resource/asset_ref.hpp"
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/resource_cache.hpp"
#include "engine/resource/texture/texture_resource.hpp"
#include <array>

namespace gneiss::engine {
class render_asset_loader;
class render_resource_service;
class resource_retirement_queue;

/** 稳定运行身份，与 material_data 的 CPU 内容寿命分离。由 Render 创建并延迟析构。 */
class material_resource final {
public:
  [[nodiscard]] const asset_identity& source() const noexcept { return identity_; }
  ~material_resource();
  /** 所属线程只读参数与依赖；更新通过 Loader/Render 受控入口执行。 */
  [[nodiscard]] const material_parameters& parameters() const noexcept { return parameters_; }
  [[nodiscard]] const std::array<texture, 5>& textures() const noexcept { return textures_; }
  material_resource(const material_resource&) = delete;
  material_resource& operator=(const material_resource&) = delete;

private:
  friend class resource_retirement_queue;
  material_resource(render_resource_service& owner, rid value,
                    std::array<texture, 5> dependencies = {}) noexcept;
  friend rid resource_handle(const asset_ref<const material_resource>&) noexcept;
  friend class render_asset_loader;
  friend class render_resource_service;
  friend rid asset_rid(const asset_ref<const resource_cache::entry>&) noexcept;
  render_resource_service* resources_;
  rid rid_;
  mutable asset_identity identity_;
  mutable std::uint64_t update_sequence_{};
  mutable material_parameters parameters_;
  mutable std::array<texture, 5> textures_;
};

using material = asset_ref<const material_resource>;

/** Render 内部标识提取；不转移所有权，不表示内容已就绪。 */
inline rid resource_handle(const material& value) noexcept {
  return value ? value->rid_ : null_rid;
}

} // namespace gneiss::engine
