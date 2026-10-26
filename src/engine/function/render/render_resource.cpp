// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_resource.hpp"
#include "engine/function/render/render_resource_service.hpp"

#include <utility>

namespace gneiss::engine {
mesh_resource::mesh_resource(render_resource_service& owner, gneiss::engine::rid value) noexcept
    : resources_(&owner), rid_(value) {}
mesh_resource::~mesh_resource() {
  if (resources_ && rid_ != null_rid)
    (void)resources_->destroy_mesh(rid_);
}
texture_resource::texture_resource(render_resource_service& owner,
                                   gneiss::engine::rid value) noexcept
    : resources_(&owner), rid_(value) {}
texture_resource::~texture_resource() {
  if (resources_ && rid_ != null_rid)
    (void)resources_->destroy_texture(rid_);
}
material_resource::material_resource(render_resource_service& owner, gneiss::engine::rid value,
                                     std::array<texture, 5> dependencies) noexcept
    : resources_(&owner), rid_(value), textures_(std::move(dependencies)) {}
material_resource::~material_resource() {
  if (resources_ && rid_ != null_rid)
    (void)resources_->destroy_material(rid_);
}
} // namespace gneiss::engine
