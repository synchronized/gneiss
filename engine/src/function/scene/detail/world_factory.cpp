// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "function/scene/world_state.hpp"
#include <atomic>
#include <gneiss/engine/function/scene/world.hpp>
#include <limits>
#include <new>

namespace gneiss {
void world_deleter::operator()(world* object) const noexcept {
  if (!object->is_owner_thread())
    std::terminate();
  delete static_cast<world_state*>(object);
}
result<world_owner> create_native() {
  // 只分配节点/ECS 所属域，不登记 World 对象，也不复用耗尽的域。
  static std::atomic_uint32_t next_domain{1U};
  auto domain = next_domain.load(std::memory_order_relaxed);
  do {
    if (domain == std::numeric_limits<std::uint32_t>::max())
      return error::out_of_memory;
  } while (!next_domain.compare_exchange_weak(domain, domain + 1U, std::memory_order_relaxed));
  return world_owner(new world_state(domain));
}
}
