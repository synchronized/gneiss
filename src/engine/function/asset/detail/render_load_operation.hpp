// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/resource/material/material_description.hpp"
#include "engine/resource/mesh/mesh_data.hpp"
#include "engine/resource/texture/texture_data.hpp"
#include "load_operation.hpp"
namespace gneiss::engine {
class asset_manager;
namespace detail {
struct asset_construction;
/** 单个渲染资产的解码观察和依赖装配；调度与停止由管理器负责。 */
struct render_load_operation final : load_operation {
  GNEISS_API void advance(asset_manager&, asset_construction&);
  std::variant<asset_request<mesh_data>, asset_request<texture_data>,
               asset_request<material_description>>
      cpu;
  asset_ref<const material_description> description;
  std::array<ref<const engine::texture>, 5> texture_references;
  std::vector<asset_request<render_resource_ref>> dependencies;
  error dependency_error{error::success};
};

} // namespace detail
} // namespace gneiss::engine
