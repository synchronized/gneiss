// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "world/system_scheduler.h"
#include "world/world_state.h"

#include <array>
#include <cstddef>
#include <new>

namespace {

struct position {
  int value;
};

struct system_context {
  std::array<int, 4> events{};
  std::size_t count = 0;
  int delta = 0;
};

gneiss_result update_position(gneiss::world_internal::world_state& world,
                              void* user_data) noexcept {
  auto& context = *static_cast<system_context*>(user_data);
  context.events[context.count++] = 1;
  try {
    world.each<position>([&context](position& value) { value.value += context.delta; });
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result fail_system(gneiss::world_internal::world_state& /*world*/,
                          void* user_data) noexcept {
  auto& context = *static_cast<system_context*>(user_data);
  context.events[context.count++] = 2;
  return GNEISS_ERROR_INVALID_STATE;
}

gneiss_result skipped_system(gneiss::world_internal::world_state& /*world*/,
                             void* user_data) noexcept {
  auto& context = *static_cast<system_context*>(user_data);
  context.events[context.count++] = 3;
  return GNEISS_SUCCESS;
}

} // namespace

int run_tests() {
  gneiss::world_internal::world_state first{1};
  gneiss::world_internal::world_state second{2};
  const auto entity = first.create_entity();
  first.emplace<position>(entity, 10);
  if (!first.is_alive(entity) || second.is_alive(entity) || first.entity_count() != 1U) {
    return 1;
  }

  system_context context{.delta = 5};
  gneiss::world_internal::system_scheduler scheduler;
  if (scheduler.add(1, update_position, &context) != GNEISS_SUCCESS ||
      scheduler.add(2, fail_system, &context) != GNEISS_SUCCESS ||
      scheduler.add(3, skipped_system, &context) != GNEISS_SUCCESS ||
      scheduler.add(3, skipped_system, &context) != GNEISS_ERROR_INVALID_ARGUMENT ||
      scheduler.run(first) != GNEISS_ERROR_INVALID_STATE) {
    return 2;
  }
  const auto* value = first.get<position>(entity);
  if (value == nullptr || value->value != 15 || context.count != 2U || context.events[0] != 1 ||
      context.events[1] != 2) {
    return 3;
  }
  if (!first.destroy_entity(entity) || first.is_alive(entity) ||
      first.get<position>(entity) != nullptr) {
    return 4;
  }
  const auto replacement = first.create_entity();
  if (replacement == entity || first.is_alive(entity) || !first.is_alive(replacement)) {
    return 5;
  }
  return 0;
}

int main() {
  try {
    return run_tests();
  } catch (...) {
    return 99;
  }
}
