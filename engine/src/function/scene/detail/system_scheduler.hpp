// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_SYSTEM_SCHEDULER_HPP_
#define GNEISS_WORLD_SYSTEM_SCHEDULER_HPP_

#include "function/scene/world_state.hpp"

#include <gneiss/engine/core/base/error.hpp>

#include <cstdint>
#include <vector>

namespace gneiss {

using system_id = std::uint32_t;
using system_callback = gneiss::error (*)(world_state& world, void* user_data) noexcept;

/** 按注册顺序串行执行 System；调用方负责外部同步。 */
class system_scheduler final {
public:
  [[nodiscard]] gneiss::error add(system_id id, system_callback callback, void* user_data);
  [[nodiscard]] gneiss::error run(world_state& world) noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return systems_.size(); }

private:
  struct entry {
    system_id id;
    system_callback callback;
    void* user_data;
  };

  std::vector<entry> systems_;
};

} // namespace gneiss

#endif
