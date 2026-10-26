// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/world/world.hpp"
#include <atomic>
#include <limits>
#include <new>

namespace gneiss::engine {
struct world_factory {
  static std::unique_ptr<world> make(std::uint32_t domain) {
    return std::unique_ptr<world>(new world(domain));
  }
};
void world_deleter::operator()(world* object) const noexcept {
  if (!object->is_owner_thread())
    std::terminate();
  delete object;
}
result create_native(world_owner& owner) noexcept {
  if (owner)
    return result::invalid_state;
  // 只分配节点/ECS 所属域，不登记 World 对象，也不复用耗尽的域。
  static std::atomic_uint32_t next_domain{1U};
  auto domain = next_domain.load(std::memory_order_relaxed);
  do {
    if (domain == std::numeric_limits<std::uint32_t>::max())
      return result::out_of_memory;
  } while (!next_domain.compare_exchange_weak(domain, domain + 1U, std::memory_order_relaxed));
  try {
    auto candidate = world_factory::make(static_cast<std::uint32_t>(domain));
    owner = world_owner(candidate.release());
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}
}
