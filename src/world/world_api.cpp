// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "core/rid_table.h"
#include "world/world_state.h"

#include <gneiss/scene.h>
#include <gneiss/world.h>

#include <limits>
#include <memory>
#include <mutex>
#include <new>

namespace {

using world_resource = std::unique_ptr<gneiss::world_internal::world_state>;
using world_table = gneiss::core::rid_table<world_resource>;

struct world_registry {
  std::mutex mutex;
  world_table worlds{1};
  std::uint32_t next_domain = 1;
};

world_registry& get_world_registry() {
  static world_registry registry;
  return registry;
}

gneiss::world_internal::world_state* find_world(world_registry& registry,
                                                gneiss_world world) noexcept {
  auto* resource = registry.worlds.get(world, gneiss::core::resource_type::world);
  return resource == nullptr ? nullptr : resource->get();
}

gneiss_result validate_world_thread(gneiss::world_internal::world_state* world) noexcept {
  if (world == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  return world->is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE;
}

} // namespace

extern "C" gneiss_result gneiss_world_create(const gneiss_world_desc* desc,
                                             gneiss_world* out_world) {
  if (out_world == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_world = GNEISS_NULL_WORLD;
  if (desc == nullptr || desc->struct_size < sizeof(gneiss_world_desc) || desc->reserved != 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    if (registry.next_domain == 0U ||
        registry.next_domain == std::numeric_limits<std::uint32_t>::max()) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
    auto state = std::make_unique<gneiss::world_internal::world_state>(registry.next_domain++);
    return registry.worlds.create(gneiss::core::resource_type::world, std::move(state), out_world);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_world_destroy(gneiss_world world) {
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    const auto thread_result = validate_world_thread(find_world(registry, world));
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    return registry.worlds.destroy(world, gneiss::core::resource_type::world);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_world_entity_create(gneiss_world world,
                                                    gneiss_entity_id* out_entity) {
  if (out_entity == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_entity = GNEISS_NULL_ENTITY_ID;
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    auto* state = find_world(registry, world);
    const auto thread_result = validate_world_thread(state);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    *out_entity = state->create_entity();
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 使用不同语义的不透明整数句柄。
extern "C" gneiss_result gneiss_world_entity_destroy(gneiss_world world, gneiss_entity_id entity) {
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    auto* state = find_world(registry, world);
    const auto thread_result = validate_world_thread(state);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    return state->destroy_entity(entity) ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_HANDLE;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 使用不同语义的不透明整数句柄。
extern "C" gneiss_result gneiss_world_entity_is_alive(gneiss_world world, gneiss_entity_id entity,
                                                      uint8_t* out_is_alive) {
  if (out_is_alive == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_is_alive = 0;
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    auto* state = find_world(registry, world);
    const auto thread_result = validate_world_thread(state);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    *out_is_alive = state->is_alive(entity) ? UINT8_C(1) : UINT8_C(0);
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_world_entity_count(gneiss_world world, uint64_t* out_count) {
  if (out_count == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_count = 0;
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    auto* state = find_world(registry, world);
    const auto thread_result = validate_world_thread(state);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    *out_count = static_cast<uint64_t>(state->entity_count());
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// C ABI 中 World、Node 与 Entity 均为定宽不透明整数，参数语义由名称和文档区分。
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
extern "C" gneiss_result gneiss_scene_node_create(gneiss_world world, gneiss_scene_node_id parent,
                                                  gneiss_entity_id entity,
                                                  gneiss_scene_node_id* out_node) {
  if (out_node == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_node = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    auto* state = find_world(registry, world);
    const auto thread_result = validate_world_thread(state);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    if (entity != GNEISS_NULL_ENTITY_ID && !state->is_alive(entity)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    return state->scene().create(parent, entity, out_node);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_node_destroy(gneiss_world world, gneiss_scene_node_id node) {
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    auto* state = find_world(registry, world);
    const auto thread_result = validate_world_thread(state);
    return thread_result == GNEISS_SUCCESS ? state->scene().destroy(node) : thread_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_node_reparent(gneiss_world world, gneiss_scene_node_id node,
                                                    gneiss_scene_node_id parent) {
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    auto* state = find_world(registry, world);
    const auto thread_result = validate_world_thread(state);
    return thread_result == GNEISS_SUCCESS ? state->scene().reparent(node, parent) : thread_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_node_set_local_transform(gneiss_world world,
                                                               gneiss_scene_node_id node,
                                                               const gneiss_transform* transform) {
  if (transform == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    auto* state = find_world(registry, world);
    const auto thread_result = validate_world_thread(state);
    return thread_result == GNEISS_SUCCESS ? state->scene().set_local(node, *transform)
                                           : thread_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_node_get_local_transform(gneiss_world world,
                                                               gneiss_scene_node_id node,
                                                               gneiss_transform* out_transform) {
  if (out_transform == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    auto* state = find_world(registry, world);
    const auto thread_result = validate_world_thread(state);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    const auto* transform = state->scene().get_local(node);
    if (transform == nullptr) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    *out_transform = *transform;
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_node_get_world_transform(gneiss_world world,
                                                               gneiss_scene_node_id node,
                                                               gneiss_transform* out_transform) {
  if (out_transform == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    auto* state = find_world(registry, world);
    const auto thread_result = validate_world_thread(state);
    return thread_result == GNEISS_SUCCESS ? state->scene().get_world(node, out_transform)
                                           : thread_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_node_get_entity(gneiss_world world, gneiss_scene_node_id node,
                                                      gneiss_entity_id* out_entity) {
  if (out_entity == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_entity = GNEISS_NULL_ENTITY_ID;
  try {
    auto& registry = get_world_registry();
    const std::scoped_lock lock{registry.mutex};
    auto* state = find_world(registry, world);
    const auto thread_result = validate_world_thread(state);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    const auto* transform = state->scene().get_local(node);
    if (transform == nullptr) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    *out_entity = state->scene().get_entity(node);
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
// NOLINTEND(bugprone-easily-swappable-parameters)
