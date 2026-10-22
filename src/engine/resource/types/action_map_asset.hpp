// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/types/action_map.hpp"
namespace gneiss::engine::resource {
/** 可共享的输入映射资源；发布后只读，实时输入状态由 InputService 单独拥有。 */
struct action_map_asset final {
  action_map map;
};
}
