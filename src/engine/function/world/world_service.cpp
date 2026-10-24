// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/world_service.hpp"
#include "engine/core/identifier_conversion.hpp"
#include "engine/core/result.hpp"

#include "engine/core/rid_table.hpp"
#include "engine/function/world/render_snapshot.hpp"
#include "engine/function/world/world.hpp"

#include <gneiss/engine/scene.h>
#include <gneiss/engine/world.h>

#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <numbers>
#include <unordered_set>

namespace gneiss::engine::world {
struct world_factory {
  static std::unique_ptr<engine::world::world> make(std::uint32_t domain) {
    return std::unique_ptr<engine::world::world>(new engine::world::world(domain));
  }
};
}

namespace {

gneiss::engine::core::math::transform native_transform(const gneiss_transform& value) noexcept {
  return {{value.translation[0], value.translation[1], value.translation[2]},
          {value.rotation[0], value.rotation[1], value.rotation[2], value.rotation[3]},
          {value.scale[0], value.scale[1], value.scale[2]}};
}
gneiss_transform boundary_transform(const gneiss::engine::core::math::transform& value) noexcept {
  return {{value.translation[0], value.translation[1], value.translation[2]},
          {value.rotation[0], value.rotation[1], value.rotation[2], value.rotation[3]},
          {value.scale[0], value.scale[1], value.scale[2]}};
}

struct world_resource {
  std::unique_ptr<gneiss::engine::world::world> owned;
  gneiss::engine::world::world* borrowed{};
  [[nodiscard]] auto* get() const noexcept { return owned ? owned.get() : borrowed; }
};
using world_table = gneiss::engine::core::rid_table<world_resource>;

struct world_registry {
  std::mutex mutex;
  world_table worlds{1};
  std::uint32_t next_domain = 1;
};

world_registry& get_world_registry() {
  static world_registry registry;
  return registry;
}

gneiss::engine::world::world* find_world(world_registry& registry, gneiss_world world) noexcept {
  auto* resource = registry.worlds.get(world, gneiss::engine::core::resource_type::world);
  return resource == nullptr ? nullptr : resource->get();
}

gneiss::engine::core::result validate_world_thread(gneiss::engine::world::world* world) noexcept {
  if (world == nullptr) {
    return gneiss::engine::core::result::invalid_handle;
  }
  return world->is_owner_thread() ? gneiss::engine::core::result::success
                                  : gneiss::engine::core::result::invalid_state;
}

template <typename Function>
gneiss::engine::core::result with_world(gneiss_world handle, Function&& function) noexcept {
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    auto* object = find_world(registry, handle);
    const auto valid = validate_world_thread(object);
    return valid == gneiss::engine::core::result::success ? function(*object) : valid;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

bool is_valid_camera(float vertical_field_of_view_radians, float near_plane,
                     float far_plane) noexcept {
  return std::isfinite(vertical_field_of_view_radians) && std::isfinite(near_plane) &&
         std::isfinite(far_plane) && vertical_field_of_view_radians > 0.0F &&
         vertical_field_of_view_radians < std::numbers::pi_v<float> && near_plane > 0.0F &&
         far_plane > near_plane;
}

} // namespace

namespace gneiss::engine::world {

gneiss::engine::core::result begin_publication(gneiss_world world,
                                               engine::core::publication& output) noexcept {
  output.reset();
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.begin_publication(output);
  });
}

gneiss::engine::core::result publish(gneiss_world world,
                                     const engine::core::publication& group) noexcept {
  return with_world(
      world, [&](engine::world::world& native_world) { return native_world.publish(group); });
}

gneiss::engine::core::result retire_publication(gneiss_world world,
                                                const engine::core::publication& group) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.retire_publication(group);
  });
}

gneiss::engine::core::result
replace_publications(gneiss_world world, std::span<const engine::core::publication> previous,
                     std::span<const engine::core::publication> candidates) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.replace_publications(previous, candidates);
  });
}

gneiss::engine::core::result bind_publication(gneiss_world world,
                                              const engine::core::publication& group,
                                              staged_object object) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.bind_publication(group, object);
  });
}

gneiss::engine::core::result create_staged_object(gneiss_world world,
                                                  const engine::core::publication& group,
                                                  const staged_object_description& description,
                                                  staged_object& output) noexcept {
  output = {};
  if (description.camera != nullptr &&
      !is_valid_camera(description.camera->vertical_field_of_view_radians,
                       description.camera->near_plane, description.camera->far_plane)) {
    return gneiss::engine::core::result::invalid_argument;
  }
  if (description.renderer != nullptr && (description.renderer->mesh == GNEISS_NULL_MESH ||
                                          description.renderer->material == GNEISS_NULL_MATERIAL)) {
    return gneiss::engine::core::result::invalid_argument;
  }
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.create_staged_object(group, description, output);
  });
}

gneiss::engine::core::result destroy_staged_object(gneiss_world world,
                                                   const engine::core::publication& group,
                                                   staged_object object) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.destroy_staged_object(group, object);
  });
}

gneiss::engine::core::result create(gneiss_world& out_world) noexcept {
  out_world = GNEISS_NULL_WORLD;
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    if (registry.next_domain == 0U ||
        registry.next_domain == std::numeric_limits<std::uint32_t>::max()) {
      return gneiss::engine::core::result::out_of_memory;
    }
    auto state = gneiss::engine::world::world_factory::make(registry.next_domain++);
    return registry.worlds.create(gneiss::engine::core::resource_type::world,
                                  world_resource{std::move(state), nullptr}, &out_world);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

void world_deleter::operator()(engine::world::world* state) const noexcept {
  if (!state->is_owner_thread())
    std::terminate();
  {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    const auto* entry = registry.worlds.get(borrowed, engine::core::resource_type::world);
    if (!entry || entry->owned || entry->borrowed != state)
      std::terminate();
    (void)registry.worlds.destroy(borrowed, engine::core::resource_type::world);
  }
  delete state;
}

gneiss::engine::core::result create_native(world_owner& owner, gneiss_world& borrowed) noexcept {
  borrowed = GNEISS_NULL_WORLD;
  if (owner)
    return gneiss::engine::core::result::invalid_state;
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    if (registry.next_domain == 0U ||
        registry.next_domain == std::numeric_limits<std::uint32_t>::max())
      return gneiss::engine::core::result::out_of_memory;
    auto candidate = world_factory::make(registry.next_domain++);
    const auto result = registry.worlds.create(engine::core::resource_type::world,
                                               world_resource{nullptr, candidate.get()}, &borrowed);
    if (result == gneiss::engine::core::result::success)
      owner = world_owner(candidate.release(), world_deleter{borrowed});
    return result;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result destroy(gneiss_world world) noexcept {
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    const auto thread_result = validate_world_thread(find_world(registry, world));
    if (thread_result != gneiss::engine::core::result::success) {
      return thread_result;
    }
    const auto* entry = registry.worlds.get(world, engine::core::resource_type::world);
    if (!entry->owned) {
      return gneiss::engine::core::result::invalid_state;
    }
    return registry.worlds.destroy(world, gneiss::engine::core::resource_type::world);
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result entity_create(gneiss_world world,
                                           gneiss_entity_id& out_entity) noexcept {
  out_entity = GNEISS_NULL_ENTITY_ID;
  return with_world(world, [&](engine::world::world& native_world) {
    return engine::core::write_identifier<entity_id>(
        out_entity, [&](auto& native_output) { return native_world.entity_create(native_output); });
  });
}

gneiss::engine::core::result entity_destroy(gneiss_world world, gneiss_entity_id entity) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.entity_destroy(gneiss::entity_id{entity});
  });
}

gneiss::engine::core::result entity_is_alive(gneiss_world world, gneiss_entity_id entity,
                                             uint8_t& out_is_alive) noexcept {
  out_is_alive = 0;
  return with_world(world, [&](engine::world::world& native_world) {
    bool value{};
    const auto result = native_world.entity_is_alive(gneiss::entity_id{entity}, value);
    if (result == gneiss::engine::core::result::success)
      out_is_alive = value ? UINT8_C(1) : UINT8_C(0);
    return result;
  });
}

gneiss::engine::core::result entity_count(gneiss_world world, uint64_t& out_count) noexcept {
  out_count = 0;
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.entity_count(out_count);
  });
}

gneiss::engine::core::result entity_set_camera(gneiss_world world, gneiss_entity_id entity,
                                               const camera_settings& camera,
                                               bool primary) noexcept {
  if (!is_valid_camera(camera.vertical_field_of_view_radians, camera.near_plane,
                       camera.far_plane)) {
    return gneiss::engine::core::result::invalid_argument;
  }
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.entity_set_camera(gneiss::entity_id{entity}, camera, primary);
  });
}

gneiss::engine::core::result entity_configure_camera(gneiss_world world, gneiss_entity_id entity,
                                                     const camera_settings& desc) noexcept {
  if (!is_valid_camera(desc.vertical_field_of_view_radians, desc.near_plane, desc.far_plane)) {
    return gneiss::engine::core::result::invalid_argument;
  }
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.entity_configure_camera(gneiss::entity_id{entity}, desc);
  });
}

gneiss::engine::core::result entity_get_camera(gneiss_world world, gneiss_entity_id entity,
                                               camera_settings& out_camera) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.entity_get_camera(gneiss::entity_id{entity}, out_camera);
  });
}

gneiss::engine::core::result entity_remove_camera(gneiss_world world,
                                                  gneiss_entity_id entity) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.entity_remove_camera(gneiss::entity_id{entity});
  });
}

gneiss::engine::core::result set_active_camera(gneiss_world world,
                                               gneiss_entity_id entity) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.set_active_camera(gneiss::entity_id{entity});
  });
}

gneiss::engine::core::result get_active_camera(gneiss_world world,
                                               gneiss_entity_id& out_entity) noexcept {
  out_entity = GNEISS_NULL_ENTITY_ID;
  return with_world(world, [&](engine::world::world& native_world) {
    return engine::core::write_identifier<entity_id>(out_entity, [&](auto& native_output) {
      return native_world.get_active_camera(native_output);
    });
  });
}

gneiss::engine::core::result
replace_mesh_renderers(gneiss_world world,
                       std::span<const mesh_renderer_replacement> replacements) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.replace_mesh_renderers(replacements);
  });
}

gneiss::engine::core::result entity_set_mesh_renderer(gneiss_world world, gneiss_entity_id entity,
                                                      const mesh_renderer& renderer) noexcept {
  if (renderer.mesh == GNEISS_NULL_MESH || renderer.material == GNEISS_NULL_MATERIAL) {
    return gneiss::engine::core::result::invalid_argument;
  }

  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.entity_set_mesh_renderer(gneiss::entity_id{entity}, renderer);
  });
}

gneiss::engine::core::result entity_remove_mesh_renderer(gneiss_world world,
                                                         gneiss_entity_id entity) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.entity_remove_mesh_renderer(gneiss::entity_id{entity});
  });
}

gneiss::engine::core::result node_create(gneiss_world world, gneiss_scene_node_id parent,
                                         gneiss_entity_id entity,
                                         gneiss_scene_node_id& out_node) noexcept {
  out_node = GNEISS_NULL_SCENE_NODE_ID;
  return with_world(world, [&](engine::world::world& native_world) {
    return engine::core::write_identifier<scene_node_id>(out_node, [&](auto& native_output) {
      return native_world.node_create(gneiss::scene_node_id{parent}, gneiss::entity_id{entity},
                                      native_output);
    });
  });
}

gneiss::engine::core::result node_destroy(gneiss_world world, gneiss_scene_node_id node) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.node_destroy(gneiss::scene_node_id{node});
  });
}

gneiss::engine::core::result node_reparent(gneiss_world world, gneiss_scene_node_id node,
                                           gneiss_scene_node_id parent) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.node_reparent(gneiss::scene_node_id{node}, gneiss::scene_node_id{parent});
  });
}

gneiss::engine::core::result node_set_local_transform(gneiss_world world, gneiss_scene_node_id node,
                                                      const gneiss_transform& transform) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.node_set_local_transform(gneiss::scene_node_id{node},
                                                 native_transform(transform));
  });
}

gneiss::engine::core::result node_get_local_transform(gneiss_world world, gneiss_scene_node_id node,
                                                      gneiss_transform& out_transform) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    engine::core::math::transform value;
    const auto result = native_world.node_get_local_transform(gneiss::scene_node_id{node}, value);
    if (result == gneiss::engine::core::result::success)
      out_transform = boundary_transform(value);
    return result;
  });
}

gneiss::engine::core::result node_get_world_transform(gneiss_world world, gneiss_scene_node_id node,
                                                      gneiss_transform& out_transform) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    engine::core::math::transform value;
    const auto result = native_world.node_get_world_transform(gneiss::scene_node_id{node}, value);
    if (result == gneiss::engine::core::result::success)
      out_transform = boundary_transform(value);
    return result;
  });
}

gneiss::engine::core::result node_get_entity(gneiss_world world, gneiss_scene_node_id node,
                                             gneiss_entity_id& out_entity) noexcept {
  out_entity = GNEISS_NULL_ENTITY_ID;
  return with_world(world, [&](engine::world::world& native_world) {
    return engine::core::write_identifier<entity_id>(out_entity, [&](auto& native_output) {
      return native_world.node_get_entity(gneiss::scene_node_id{node}, native_output);
    });
  });
}

gneiss::engine::core::result node_get_parent(gneiss_world world, gneiss_scene_node_id node,
                                             gneiss_scene_node_id& out_parent) noexcept {
  out_parent = GNEISS_NULL_SCENE_NODE_ID;
  return with_world(world, [&](engine::world::world& native_world) {
    return engine::core::write_identifier<scene_node_id>(out_parent, [&](auto& native_output) {
      return native_world.node_get_parent(gneiss::scene_node_id{node}, native_output);
    });
  });
}

gneiss::engine::core::result entity_get_local_transform(gneiss_world world, gneiss_entity_id entity,
                                                        gneiss_transform& out_transform) noexcept {
  out_transform = GNEISS_TRANSFORM_IDENTITY;
  return with_world(world, [&](engine::world::world& native_world) {
    engine::core::math::transform value;
    const auto result = native_world.entity_get_local_transform(gneiss::entity_id{entity}, value);
    if (result == gneiss::engine::core::result::success)
      out_transform = boundary_transform(value);
    return result;
  });
}

gneiss::engine::core::result
entity_set_local_transform(gneiss_world world, gneiss_entity_id entity,
                           const gneiss_transform& transform) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.entity_set_local_transform(gneiss::entity_id{entity},
                                                   native_transform(transform));
  });
}

gneiss::engine::core::result
get_render_snapshot(gneiss_world world, std::uint32_t viewport_width, std::uint32_t viewport_height,
                    engine::render::render_snapshot& out_snapshot,
                    const engine::render::view_camera* camera) noexcept {
  return with_world(world, [&](engine::world::world& native_world) {
    return native_world.get_render_snapshot(viewport_width, viewport_height, out_snapshot, camera);
  });
}

} // namespace gneiss::engine::world
