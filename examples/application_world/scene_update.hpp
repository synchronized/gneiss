// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/entity_id.hpp"
#include "engine/core/result.hpp"
namespace gneiss::runtime {
class application;
}
namespace example {
struct scene_state {
  gneiss::entity_id entity;
  unsigned updates{};
};
gneiss::engine::result update(gneiss::runtime::application& app, scene_state& state) noexcept;
}
