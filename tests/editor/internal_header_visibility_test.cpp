// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/application.hpp"
// 只包含 Application 不应引入具体 Scene 服务或输入服务。
#if defined(GNEISS_SCENE_SCENE_INSTANCE_SERVICE_HPP_) ||                                           \
    defined(GNEISS_INPUT_INPUT_SERVICE_HPP_) || defined(GNEISS_RENDER_RENDER_EXECUTOR_HPP_)
#error "Application 重新传递包含了具体服务"
#endif
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"
#include "panels/property_inspector_model.hpp"
#include "runtime/application_bridge.hpp"

// 与真实宿主使用相同传递依赖；私有头重新泄漏时编译必须失败。
#if __has_include("runtime/application_registry.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("runtime/loading/application_scene_state.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/resource/asset_manager.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("runtime/loading/scene_load_service.hpp")
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
// 原生描述结构不能退回裸 ID，否则方法签名的类型约束会被绕过。
static_assert(
    std::is_same_v<decltype(gneiss::scene_internal::node_creation::parent), gneiss::scene_node_id>);
static_assert(std::is_same_v<decltype(gneiss::scene_internal::prefab_creation::parent),
                             gneiss::scene_node_id>);
static_assert(std::is_same_v<decltype(gneiss::scene_internal::mesh_renderer_node_creation::parent),
                             gneiss::scene_node_id>);
static_assert(
    std::is_same_v<decltype(gneiss::world_internal::staged_object::entity), gneiss::entity_id>);
static_assert(
    std::is_same_v<decltype(gneiss::world_internal::staged_object::node), gneiss::scene_node_id>);
static_assert(std::is_same_v<decltype(gneiss::world_internal::staged_object_description::parent),
                             gneiss::scene_node_id>);
static_assert(std::is_same_v<decltype(gneiss::world_internal::mesh_renderer_replacement::entity),
                             gneiss::entity_id>);
static_assert(
    std::is_same_v<decltype(gneiss::scene_internal::prefab_property_override_key::type_id),
                   gneiss::type_id>);
static_assert(
    std::is_same_v<decltype(gneiss::scene_internal::prefab_property_override_key::field_id),
                   gneiss::field_id>);
template <typename T>
concept exposes_texture_service = requires(T& value) { value.texture_service(); };
template <typename T>
concept exposes_asset_loader = requires(T& value) { value.asset_loader(); };
static_assert(!exposes_texture_service<gneiss::runtime::application>);
static_assert(!exposes_asset_loader<gneiss::runtime::application>);
int main() { return 0; }
