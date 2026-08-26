// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_INSTANCE_SERVICE_H_
#define GNEISS_SCENE_SCENE_INSTANCE_SERVICE_H_

#include "core/rid_table.h"
#include "render/render_asset_loader.h"

#include <gneiss/scene.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::asset_internal {
class virtual_file_system;
}

namespace gneiss::scene_internal {

class scene_instance final {
public:
  scene_instance(gneiss_world world, render_internal::render_asset_loader& loader) noexcept;
  ~scene_instance() noexcept;

  scene_instance(const scene_instance&) = delete;
  scene_instance& operator=(const scene_instance&) = delete;

  struct object final {
    std::string uuid;
    gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
    gneiss_scene_node_id node = GNEISS_NULL_SCENE_NODE_ID;
    render_internal::mesh_asset_lease mesh;
    render_internal::material_asset_lease material;
  };

  void rollback() noexcept;
  [[nodiscard]] gneiss_scene_node_id find_node(std::string_view uuid) const noexcept;

  std::vector<object> objects;

private:
  gneiss_world world_;
  render_internal::render_asset_loader& loader_;
};

class scene_instance_service final {
public:
  scene_instance_service(gneiss_world world, const asset_internal::virtual_file_system& file_system,
                         render_internal::render_asset_loader& loader) noexcept;

  [[nodiscard]] bool is_valid() const noexcept { return domain_ != 0U; }
  [[nodiscard]] gneiss_result load(std::string_view uri,
                                   gneiss_scene_instance* out_instance) noexcept;
  [[nodiscard]] gneiss_result unload(gneiss_scene_instance instance) noexcept;
  [[nodiscard]] gneiss_result find_node(gneiss_scene_instance instance, std::string_view uuid,
                                        gneiss_scene_node_id* out_node) const noexcept;

private:
  using instance_ptr = std::unique_ptr<scene_instance>;
  gneiss_world world_;
  const asset_internal::virtual_file_system& file_system_;
  render_internal::render_asset_loader& loader_;
  std::uint16_t domain_;
  core::rid_table<instance_ptr> instances_;
};

} // namespace gneiss::scene_internal

#endif
