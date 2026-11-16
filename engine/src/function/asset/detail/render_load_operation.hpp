// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/resource/material/material_description.hpp>
#include <gneiss/engine/resource/mesh/mesh_data.hpp>
#include <gneiss/engine/resource/texture/texture_data.hpp>
#include "load_operation.hpp"
#include "reference_dependency.hpp"
namespace gneiss {
class asset_manager;
namespace detail {
/** 单个渲染资产的解码观察和依赖装配；调度与停止由管理器负责。 */
struct render_load_operation final : load_operation {
  render_resource_source source;
  bool reload{}, started{};
  GNEISS_API void advance(asset_manager&);
  std::variant<asset_request<mesh_data>, asset_request<texture_data>,
               asset_request<material_description>>
      cpu;
  std::shared_ptr<const mesh_data> pending_mesh;
  std::shared_ptr<const texture_data> pending_texture;
  asset_ref<const material_description> description;
  std::array<ref<const gneiss::texture>, 5> texture_references;
  std::array<std::shared_ptr<reference_dependency<gneiss::texture>>, 5> texture_dependencies;
  std::vector<std::shared_ptr<asset_dependency>> dependencies;
};

} // namespace detail
} // namespace gneiss
