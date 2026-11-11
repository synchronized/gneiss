// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/rid_table.hpp"
#include "engine/function/render/detail/render_scene_state.hpp"
#include "engine/function/render/render_service.hpp"
#include <memory>
#include <vector>
namespace gneiss {
struct render_service::resource_storage {
  explicit resource_storage(std::uint64_t limit);
  template <typename Data> struct record {
    explicit record(std::shared_ptr<const Data> value)
        : identity(std::make_shared<detail::resource_identity<Data>>()) {
      identity->publish(std::move(value));
    }
    explicit record(std::shared_ptr<detail::resource_identity<Data>> value)
        : identity(std::move(value)) {}
    std::shared_ptr<detail::resource_identity<Data>> identity;
  };
  result<std::array<std::shared_ptr<const detail::resource_identity<texture_data>>, 5>>
  material_dependencies(const material_data& value,
                        const detail::resource_identity<material_data>* previous = nullptr) const;
  std::uint64_t memory_limit_{};
  mutable std::vector<std::weak_ptr<const mesh_data>> mesh_history_;
  mutable std::vector<std::weak_ptr<const material_data>> material_history_;
  mutable std::vector<std::weak_ptr<const texture_data>> texture_history_;
  std::uint16_t domain_{};
  rid_table<record<mesh_data>> meshes_;
  rid_table<record<material_data>> materials_;
  rid_table<record<texture_data>> textures_;
  std::shared_ptr<detail::render_scene_state> scenes_;
};
} // namespace gneiss
