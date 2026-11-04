// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/rid_table.hpp"
#include "engine/function/render/render_service.hpp"
#include <memory>
#include <vector>
namespace gneiss::engine {
struct render_service::resource_storage {
  explicit resource_storage(std::uint64_t limit);
  template <typename Data> struct record {
    explicit record(std::shared_ptr<const Data> value) : data(std::move(value)) {}
    std::shared_ptr<const Data> data;
  };
  std::uint64_t memory_limit_{};
  mutable std::vector<std::weak_ptr<const mesh_data>> mesh_history_;
  mutable std::vector<std::weak_ptr<const material_data>> material_history_;
  mutable std::vector<std::weak_ptr<const texture_data>> texture_history_;
  std::uint16_t domain_{};
  rid_table<record<mesh_data>> meshes_;
  rid_table<record<material_data>> materials_;
  rid_table<record<texture_data>> textures_;
};
} // namespace gneiss::engine
