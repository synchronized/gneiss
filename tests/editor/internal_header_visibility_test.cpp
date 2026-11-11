// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/instantiation.hpp"
#include "runtime/application.hpp"
// 只包含 Application 不应引入具体 Scene 服务或输入服务。
#if defined(GNEISS_SCENE_SCENE_INSTANCES_HPP_) || defined(GNEISS_INPUT_INPUT_SERVICE_HPP_) ||      \
    defined(GNEISS_RENDER_RENDER_EXECUTOR_HPP_)
#error "Application 重新传递包含了具体服务"
#endif
#include "commands/author_property_edit.hpp"
#include "engine/core/meta/transform_properties.hpp"
#include "engine/function/scene/world.hpp"

// 与真实宿主使用相同传递依赖；私有头重新泄漏时编译必须失败。
#if __has_include("engine/resource/texture/detail/texture_resource_access.hpp") ||                 \
                  __has_include(                                                                   \
                      "engine/resource/mesh/detail/mesh_resource_access.hpp") ||                   \
                      __has_include(                                                               \
                          "engine/resource/material/detail/material_resource_access.hpp")
#error "资源私有访问器泄漏到宿主"
#endif

#if __has_include("engine/function/asset/detail/resource_load_group.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/function/asset/resource_cache.hpp")
#error "资产缓存实现泄漏到宿主"
#endif
#if __has_include("engine/function/asset/detail/asset_decode.hpp") ||                              \
                  __has_include("engine/function/asset/detail/asset_construction.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("runtime/loading/scene_domain.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/core/meta/registration.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/function/scene/detail/world_state.hpp") ||                               \
                  __has_include(                                                                   \
                      "engine/function/scene/detail/object_state.hpp") ||                          \
                      __has_include("engine/function/scene/detail/camera_selection_state.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/function/world/world_service.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/function/render/render_resource_storage.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/platform/io/uv/uv_file_system.hpp")
#error "宿主获得了未授权的内部实现头"
#endif
#if __has_include("engine/function/scene/scene_instance_state.hpp")
#error "宿主获得了场景实例的私有实现头"
#endif
#include "engine/resource/scene/prefab_description.hpp"
#include <type_traits>
static_assert(!std::is_convertible_v<gneiss::scene_instance_id, gneiss::entity_id>);
static_assert(!std::is_convertible_v<std::uint64_t, gneiss::entity_id>);
template <typename T>
concept exposes_entity_lifecycle =
    requires(T& value, gneiss::entity_id id) { value.entity_destroy(id); };
static_assert(!exposes_entity_lifecycle<gneiss::engine::world>);

static_assert(std::is_same_v<decltype(gneiss::engine::prefab_property_target::target_type_guid),
                             gneiss::engine::guid>);
static_assert(std::is_same_v<decltype(gneiss::engine::prefab_property_override_key::path),
                             gneiss::engine::meta::property_path>);
template <typename T>
concept exposes_texture_service = requires(T& value) { value.texture_service(); };
template <typename T>
concept exposes_asset_loader = requires(T& value) { value.asset_loader(); };
static_assert(!exposes_texture_service<gneiss::runtime::application>);
static_assert(!exposes_asset_loader<gneiss::runtime::application>);
template <typename T>
concept exposes_resource_factory = requires(T& value) { value.resource_factory(); };
static_assert(!exposes_resource_factory<gneiss::runtime::application>);
static_assert(std::is_same_v<decltype(std::declval<gneiss::runtime::application&>().assets()),
                             gneiss::engine::asset_manager&>);
// Render 服务是允许的 RID 边界，具体资源表和引用关联仍不可见。
#include "engine/function/render/render_service.hpp"
template <typename T>
concept exposes_resource_table = requires(T& value) { value.assets(); };
static_assert(std::is_same_v<decltype(std::declval<gneiss::runtime::application&>().assets()),
                             gneiss::engine::asset_manager&>);
static_assert(!exposes_resource_table<gneiss::engine::render_service>);
template <typename T>
concept exposes_asset_update = requires { &T::prepare_update; };
template <typename T>
concept exposes_asset_claim = requires { &T::claim_update; };
static_assert(!exposes_asset_update<gneiss::engine::asset_manager>);
static_assert(!exposes_asset_claim<gneiss::engine::asset_manager>);
template <typename T>
concept exposes_render_attachment = requires { &T::attach_render; };
static_assert(exposes_render_attachment<gneiss::engine::asset_manager>);
static_assert(!exposes_asset_update<gneiss::engine::asset_manager>);
static_assert(!exposes_asset_claim<gneiss::engine::asset_manager>);
int main() { return 0; }

// 管理器实现类型及缓存只允许内部装配代码访问。
template <class T>
concept exposes_implementation = requires { typename T::implementation; };
template <class T>
concept exposes_cache = requires(T& value) { value.cache(); };
static_assert(!exposes_implementation<gneiss::engine::asset_manager>);
static_assert(!exposes_cache<gneiss::engine::asset_manager>);
