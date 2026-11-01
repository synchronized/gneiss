// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/entity_id.hpp"
#include "engine/core/error.hpp"
namespace gneiss::runtime {
class application;
}
namespace example {
struct scene_state {
  gneiss::entity_id entity;
  unsigned updates{};
};
gneiss::engine::error update(gneiss::runtime::application& app, scene_state& state) noexcept;
}
