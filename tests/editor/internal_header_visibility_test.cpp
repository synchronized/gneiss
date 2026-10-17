// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/application/application.hpp"
#include "engine/function/application/application_bridge.hpp"
#include "engine/function/world/world_reflection.hpp"
#include "property_inspector_model.hpp"

// 与真实宿主使用相同传递依赖；私有头重新泄漏时编译必须失败。
#if __has_include("engine/function/application/application_registry.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/function/application/application_scene_state.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/function/application/application_asset_state.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/function/application/scene_load_service.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/core/reflection/type_registry.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/function/world/world_state.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/function/world/world_service.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/function/render/backend/granit/granit_render_service.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/platform/io/uv/uv_file_system.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/function/scene/scene_instance.hpp") ||                                   \
                  __has_include("engine/function/scene/prefab_runtime_instance.hpp")
#error "宿主获得了场景实例的私有实现头"
#endif
#include "engine/function/scene/prefab_candidate.hpp"
#include <type_traits>
static_assert(!std::is_copy_constructible_v<gneiss::scene_internal::prefab_candidate>);
static_assert(std::is_nothrow_move_constructible_v<gneiss::scene_internal::prefab_candidate>);
static_assert(!std::is_convertible_v<gneiss::entity_id, gneiss::scene_node_id>);
static_assert(!std::is_convertible_v<std::uint64_t, gneiss::scene_node_id>);
static_assert(std::is_invocable_v<decltype(&gneiss::engine::world::entity_destroy),
                                  gneiss::engine::world&, gneiss::entity_id>);
static_assert(!std::is_invocable_v<decltype(&gneiss::engine::world::entity_destroy),
                                   gneiss::engine::world&, gneiss::scene_node_id>);
int main() { return 0; }
