// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_SYSTEM_SCHEDULER_HPP_
#define GNEISS_WORLD_SYSTEM_SCHEDULER_HPP_

#include "engine/function/world/world_state.hpp"

#include <gneiss/engine/core/result.h>

#include <cstdint>
#include <vector>

namespace gneiss::world_internal {

using system_id = std::uint32_t;
using system_callback = gneiss_result (*)(world_state& world, void* user_data) noexcept;

/** 按注册顺序串行执行 System；调用方负责外部同步。 */
class system_scheduler final {
public:
  [[nodiscard]] gneiss_result add(system_id id, system_callback callback, void* user_data) noexcept;
  [[nodiscard]] gneiss_result run(world_state& world) noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return systems_.size(); }

private:
  struct entry {
    system_id id;
    system_callback callback;
    void* user_data;
  };

  std::vector<entry> systems_;
};

} // namespace gneiss::world_internal

#endif
