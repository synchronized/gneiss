// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

// 此目标不接收 Granit 的包含目录，验证服务接口可独立包含。
#include "engine/function/render/render_service.hpp"
#include <type_traits>
static_assert(!std::is_copy_constructible_v<gneiss::engine::render_service>);
static_assert(!std::is_move_constructible_v<gneiss::engine::render_service>);
static_assert(std::is_default_constructible_v<gneiss::engine::render_device_stats>);
