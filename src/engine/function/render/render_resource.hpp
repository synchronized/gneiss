// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/rid.hpp"
#include "engine/resource/asset_ref.hpp"
#include "engine/resource/cache/resource_cache.hpp"
#include "engine/resource/types/material_parameters.hpp"

#include <array>
#include <cstdint>

namespace gneiss::engine {
class render_asset_loader;
class render_resource_service;
class resource_retirement_queue;

/** 稳定运行身份，与 mesh_data 的 CPU 内容寿命分离。由 Render 创建并延迟析构。 */
class mesh_resource final {
public:
  ~mesh_resource();
  mesh_resource(const mesh_resource&) = delete;
  mesh_resource& operator=(const mesh_resource&) = delete;

private:
  friend class resource_retirement_queue;
  mesh_resource(render_resource_service& owner, gneiss::engine::rid value) noexcept;
  friend class render_asset_loader;
  friend class render_resource_service;
  friend gneiss::engine::rid asset_rid(const asset_ref<const resource_cache::entry>&) noexcept;
  render_resource_service* resources_;
  gneiss::engine::rid rid_;
  mutable std::uint64_t update_sequence_{};
};

/** 稳定运行身份，与 texture_data 的 CPU 内容寿命分离。由 Render 创建并延迟析构。 */
class texture_resource final {
public:
  ~texture_resource();
  texture_resource(const texture_resource&) = delete;
  texture_resource& operator=(const texture_resource&) = delete;

private:
  friend class resource_retirement_queue;
  texture_resource(render_resource_service& owner, gneiss::engine::rid value) noexcept;
  friend class render_asset_loader;
  friend class render_resource_service;
  friend gneiss::engine::rid asset_rid(const asset_ref<const resource_cache::entry>&) noexcept;
  render_resource_service* resources_;
  gneiss::engine::rid rid_;
  mutable std::uint64_t update_sequence_{};
};

using mesh = asset_ref<const mesh_resource>;
using texture = asset_ref<const texture_resource>;

/** 稳定运行身份，与 material_data 的 CPU 内容寿命分离。由 Render 创建并延迟析构。 */
class material_resource final {
public:
  ~material_resource();
  /** 所属线程只读参数与依赖；更新通过 Loader/Render 受控入口执行。 */
  [[nodiscard]] const material_parameters& parameters() const noexcept { return parameters_; }
  [[nodiscard]] const std::array<texture, 5>& textures() const noexcept { return textures_; }
  material_resource(const material_resource&) = delete;
  material_resource& operator=(const material_resource&) = delete;

private:
  friend class resource_retirement_queue;
  material_resource(render_resource_service& owner, gneiss::engine::rid value,
                    std::array<texture, 5> dependencies = {}) noexcept;
  friend class render_asset_loader;
  friend class render_resource_service;
  friend gneiss::engine::rid asset_rid(const asset_ref<const resource_cache::entry>&) noexcept;
  render_resource_service* resources_;
  gneiss::engine::rid rid_;
  mutable std::uint64_t update_sequence_{};
  mutable material_parameters parameters_;
  mutable std::array<texture, 5> textures_;
};

// 对外只读；内容更新由 Loader 和 Render 的所属线程入口执行。
using material = asset_ref<const material_resource>;

} // namespace gneiss::engine
