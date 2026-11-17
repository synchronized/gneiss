// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

// 此目标不接收 Granit 的包含目录，验证服务接口可独立包含。
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/core/window/service.hpp>
#include <type_traits>
static_assert(!std::is_copy_constructible_v<gneiss::render::service>);
static_assert(!std::is_move_constructible_v<gneiss::render::service>);
static_assert(std::is_default_constructible_v<gneiss::render::device_stats>);

static_assert(!std::is_copy_constructible_v<gneiss::window_service>);
static_assert(!std::is_move_constructible_v<gneiss::window_service>);
