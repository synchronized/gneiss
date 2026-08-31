// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "game_context_internal.h"

#include "core/rid_table.h"

#include <mutex>
#include <thread>

namespace {

struct context_state final {
  gneiss_application application;
  gneiss_world world;
  gneiss_entity_id startup_root_entity;
  std::thread::id owner_thread;
};

constexpr std::uint16_t context_domain = UINT16_C(0x4743);
std::mutex context_mutex;
gneiss::core::rid_table<context_state> contexts(context_domain);

[[nodiscard]] context_state* get_context(gneiss_game_context context) noexcept {
  auto* state = contexts.get(context, gneiss::core::resource_type::game_context);
  if (state == nullptr || state->owner_thread != std::this_thread::get_id()) {
    return nullptr;
  }
  return state;
}

} // namespace

namespace gneiss::game_internal {

gneiss_result create_game_context(gneiss_application application,
                                  gneiss_entity_id startup_root_entity,
                                  gneiss_game_context* out_context) noexcept {
  if (application == GNEISS_NULL_APPLICATION || out_context == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_context = GNEISS_NULL_GAME_CONTEXT;
  gneiss_world world = GNEISS_NULL_WORLD;
  const auto world_result = gneiss_application_get_world(application, &world);
  if (world_result != GNEISS_SUCCESS) {
    return world_result;
  }
  std::scoped_lock lock(context_mutex);
  return contexts.create(
      core::resource_type::game_context,
      context_state{application, world, startup_root_entity, std::this_thread::get_id()},
      out_context);
}

gneiss_result destroy_game_context(gneiss_game_context context) noexcept {
  std::scoped_lock lock(context_mutex);
  auto* state = get_context(context);
  if (state == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  return contexts.destroy(context, core::resource_type::game_context);
}

} // namespace gneiss::game_internal

extern "C" gneiss_result gneiss_game_context_get_world(gneiss_game_context context,
                                                       gneiss_world* out_world) {
  if (out_world == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_world = GNEISS_NULL_WORLD;
  std::scoped_lock lock(context_mutex);
  const auto* state = get_context(context);
  if (state == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  *out_world = state->world;
  return GNEISS_SUCCESS;
}

extern "C" gneiss_result gneiss_game_context_get_startup_root_entity(gneiss_game_context context,
                                                                     gneiss_entity_id* out_entity) {
  if (out_entity == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_entity = GNEISS_NULL_ENTITY_ID;
  std::scoped_lock lock(context_mutex);
  const auto* state = get_context(context);
  if (state == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  *out_entity = state->startup_root_entity;
  return GNEISS_SUCCESS;
}

extern "C" gneiss_result gneiss_game_context_find_action(gneiss_game_context context,
                                                         const char* name, uint64_t name_length,
                                                         gneiss_action* out_action) {
  std::scoped_lock lock(context_mutex);
  const auto* state = get_context(context);
  return state == nullptr
             ? GNEISS_ERROR_INVALID_HANDLE
             : gneiss_application_find_action(state->application, name, name_length, out_action);
}

extern "C" gneiss_result gneiss_game_context_get_action_state(gneiss_game_context context,
                                                              gneiss_action action,
                                                              gneiss_action_state* out_state) {
  std::scoped_lock lock(context_mutex);
  const auto* state = get_context(context);
  return state == nullptr
             ? GNEISS_ERROR_INVALID_HANDLE
             : gneiss_application_get_action_state(state->application, action, out_state);
}

extern "C" gneiss_result gneiss_game_context_request_exit(gneiss_game_context context) {
  std::scoped_lock lock(context_mutex);
  const auto* state = get_context(context);
  return state == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                          : gneiss_application_request_exit(state->application);
}
