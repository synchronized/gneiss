// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/scene/world.hpp>

// 同时检查原公开位置与真实私有位置，避免移动后检查失去意义。
#if __has_include(<gneiss/engine/function/asset/resource_cache.hpp>) || \
    __has_include("function/asset/resource_cache.hpp") || \
    __has_include(<gneiss/engine/function/application/main_loop_wakeup.hpp>) || \
    __has_include("function/application/main_loop_wakeup.hpp") || \
    __has_include(<gneiss/engine/core/types/rid_table.hpp>) || \
    __has_include("core/rid_table.hpp")
#error "Engine 向普通消费者泄漏了私有实现头"
#endif
int main() { return 0; }
