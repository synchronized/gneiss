// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/application.h>
#include <gneiss/engine/scene.h>
#include <gneiss/engine/world.h>
namespace example {
struct scene_state {
  gneiss_entity_id entity{};
  unsigned updates{};
};
gneiss_result update(gneiss_application application, const gneiss_frame_time* time,
                     void* user_data) noexcept;
}
