// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "runtime/game/game_context_internal.hpp"

#include "runtime/application.hpp"

#include "engine/core/rid_table.hpp"

#include <mutex>
#include <new>
#include <string>
#include <thread>

namespace {

struct context_state final {
  std::weak_ptr<gneiss::runtime::application> application;
  gneiss_world world;
  gneiss_entity_id startup_root_entity;
  std::thread::id owner_thread;
  std::string log_source;
};

constexpr std::uint16_t context_domain = UINT16_C(0x4743);
std::mutex context_mutex;
gneiss::engine::rid_table<context_state> contexts(context_domain);

[[nodiscard]] context_state* get_context(gneiss_game_context context) noexcept {
  auto* state = contexts.get(context, gneiss::engine::resource_type::game_context);
  if (state == nullptr || state->owner_thread != std::this_thread::get_id()) {
    return nullptr;
  }
  return state;
}

} // namespace

namespace gneiss::runtime {

gneiss::engine::result create_game_context(const std::shared_ptr<application>& parent,
                                           gneiss_entity_id startup_root_entity,
                                           gneiss_game_context* out_context) noexcept try {
  if (!parent || out_context == nullptr) {
    return gneiss::engine::result::invalid_argument;
  }
  *out_context = GNEISS_NULL_GAME_CONTEXT;
  if (!parent->is_owner_thread() || !parent->accepts_requests()) {
    return gneiss::engine::result::invalid_state;
  }
  const auto world = parent->world();
  std::scoped_lock lock(context_mutex);
  return contexts.create(gneiss::engine::resource_type::game_context,
                         context_state{
                             .application = parent,
                             .world = world,
                             .startup_root_entity = startup_root_entity,
                             .owner_thread = std::this_thread::get_id(),
                             .log_source = "game_module",
                         },
                         out_context);
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

gneiss::engine::result destroy_game_context(gneiss_game_context context) noexcept try {
  std::scoped_lock lock(context_mutex);
  auto* state = get_context(context);
  if (state == nullptr) {
    return gneiss::engine::result::invalid_handle;
  }
  return contexts.destroy(context, gneiss::engine::resource_type::game_context);
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

gneiss::engine::result set_game_context_log_source(gneiss_game_context context,
                                                   std::string_view source) noexcept {
  if (source.empty()) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    const std::scoped_lock lock(context_mutex);
    auto* state = get_context(context);
    if (state == nullptr) {
      return gneiss::engine::result::invalid_handle;
    }
    state->log_source.assign(source);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

} // namespace gneiss::runtime

namespace gneiss::runtime {
gneiss::engine::result query_context(gneiss_game_context context, context_view& output) noexcept {
  output = {};
  try {
    const std::scoped_lock lock(context_mutex);
    const auto* state = get_context(context);
    if (state == nullptr) {
      return gneiss::engine::result::invalid_handle;
    }
    auto parent = state->application.lock();
    if (!parent || !parent->accepts_requests()) {
      return gneiss::engine::result::invalid_handle;
    }
    output = {.application = std::move(parent),
              .world = state->world,
              .startup_root_entity = state->startup_root_entity};
    return gneiss::engine::result::success;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result submit_context_log(gneiss_game_context context,
                                          const gneiss::engine::message_view& message) noexcept {
  try {
    const std::scoped_lock lock(context_mutex);
    const auto* state = contexts.get(context, gneiss::engine::resource_type::game_context);
    if (state == nullptr) {
      return gneiss::engine::result::invalid_handle;
    }
    auto parent = state->application.lock();
    if (!parent || !parent->accepts_requests()) {
      return gneiss::engine::result::invalid_handle;
    }
    auto value = message;
    value.context = context;
    value.source = state->log_source;
    return parent->submit_log(value);
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
} // namespace gneiss::runtime
