// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/application.hpp>

namespace example {

// 只保存借用 ID；World 与实体由 Application 管理。
struct scene_state {
  gneiss::entity_id entity;
  std::uint64_t updates{};
};

gneiss::result update(gneiss::application_ref app, const gneiss::frame_time& time,
                      void* user_data) noexcept;

} // namespace example
