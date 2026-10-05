// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/game/game_context_internal.hpp"

#include "engine/function/application/application_registry.hpp"
#include "engine/function/application/application_state.hpp"

#include "engine/function/application/application_log_internal.hpp"
#include "engine/core/rid_table.hpp"

#include <mutex>
#include <new>
#include <string>
#include <thread>

namespace {

struct context_state final {
  gneiss_application application;
  gneiss_world world;
  gneiss_entity_id startup_root_entity;
  std::thread::id owner_thread;
  std::string log_source;
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
                                  gneiss_game_context* out_context) noexcept try {
  if (application == GNEISS_NULL_APPLICATION || out_context == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_context = GNEISS_NULL_GAME_CONTEXT;
  gneiss_world world = GNEISS_NULL_WORLD;
  const auto parent = application_internal::find_application(application);
  const auto world_result = application_internal::validate_application(parent);
  if (world_result != GNEISS_SUCCESS) {
    return world_result;
  }
  world = parent->world();
  std::scoped_lock lock(context_mutex);
  return contexts.create(core::resource_type::game_context,
                         context_state{
                             .application = application,
                             .world = world,
                             .startup_root_entity = startup_root_entity,
                             .owner_thread = std::this_thread::get_id(),
                             .log_source = "game_module",
                         },
                         out_context);
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}

gneiss_result destroy_game_context(gneiss_game_context context) noexcept try {
  std::scoped_lock lock(context_mutex);
  auto* state = get_context(context);
  if (state == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  return contexts.destroy(context, core::resource_type::game_context);
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}

gneiss_result set_game_context_log_source(gneiss_game_context context,
                                          std::string_view source) noexcept {
  if (source.empty()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    const std::scoped_lock lock(context_mutex);
    auto* state = get_context(context);
    if (state == nullptr) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    state->log_source.assign(source);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::game_internal

namespace gneiss::game_internal {
gneiss_result query_context(gneiss_game_context context, context_view& output) noexcept {
  output = {};
  try {
    const std::scoped_lock lock(context_mutex);
    const auto* state = get_context(context);
    if (state == nullptr) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    output = {.application = state->application,
              .world = state->world,
              .startup_root_entity = state->startup_root_entity};
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result submit_context_log(gneiss_game_context context,
                                 const gneiss_log_message& message) noexcept {
  try {
    const std::scoped_lock lock(context_mutex);
    const auto* state = contexts.get(context, core::resource_type::game_context);
    return state == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                            : application_internal::submit_application_log(
                                  state->application, message, state->log_source);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
} // namespace gneiss::game_internal
