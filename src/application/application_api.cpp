// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/application_log_internal.h"
#include "application/application_state.h"
#include "core/rid_table.h"

#include <gneiss/application.h>
#include <gneiss/input.h>
#include <gneiss/render.h>
#include <gneiss/scene.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>

namespace {

using application_resource = std::shared_ptr<gneiss::application_internal::application_state>;
using application_table = gneiss::core::rid_table<application_resource>;

struct application_registry {
  std::mutex mutex;
  application_table applications{2};
};

application_registry& get_application_registry() {
  static application_registry registry;
  return registry;
}

application_resource find_application(gneiss_application application) noexcept {
  auto& registry = get_application_registry();
  const std::scoped_lock lock{registry.mutex};
  const auto* resource =
      registry.applications.get(application, gneiss::core::resource_type::application);
  return resource == nullptr ? nullptr : *resource;
}

gneiss_result validate_application(const application_resource& application) noexcept {
  if (application == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  return application->is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE;
}

void report_create_failure(const gneiss_application_desc& desc, gneiss_result result,
                           std::string_view module, std::string_view message) noexcept {
  if (desc.diagnostic == nullptr) {
    return;
  }
  const gneiss_diagnostic diagnostic = {
      .struct_size = sizeof(gneiss_diagnostic),
      .severity = GNEISS_DIAGNOSTIC_ERROR,
      .category = GNEISS_DIAGNOSTIC_CATEGORY_APPLICATION,
      .result = result,
      .module = module.data(),
      .module_length = module.size(),
      .message = message.data(),
      .message_length = message.size(),
      .reserved = {},
  };
  desc.diagnostic(GNEISS_NULL_APPLICATION, &diagnostic, desc.user_data);
}

} // namespace

extern "C" gneiss_result gneiss_application_create(const gneiss_application_desc* desc,
                                                   gneiss_application* out_application) {
  if (out_application == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_application = GNEISS_NULL_APPLICATION;
  if (desc == nullptr || desc->struct_size < GNEISS_APPLICATION_DESC_VERSION_1_SIZE ||
      desc->reserved != 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  gneiss_application_desc normalized_desc = GNEISS_APPLICATION_DESC_INIT;
  std::memcpy(&normalized_desc, desc,
              std::min<std::size_t>(desc->struct_size, sizeof(gneiss_application_desc)));
  if (normalized_desc.platform > GNEISS_APPLICATION_PLATFORM_GRANIT ||
      normalized_desc.asset_reserved != 0U ||
      (normalized_desc.window_title == nullptr && normalized_desc.window_title_length != 0U) ||
      (normalized_desc.window_flags &
       ~(GNEISS_APPLICATION_WINDOW_VISIBLE_BIT | GNEISS_APPLICATION_WINDOW_RESIZABLE_BIT |
         GNEISS_APPLICATION_WINDOW_HIGH_DPI_BIT)) != 0U ||
      (normalized_desc.platform == GNEISS_APPLICATION_PLATFORM_GRANIT &&
       (normalized_desc.window_width == 0U || normalized_desc.window_height == 0U ||
        normalized_desc.initialize != nullptr || normalized_desc.poll_events != nullptr ||
        normalized_desc.shutdown != nullptr))) {
    report_create_failure(normalized_desc, GNEISS_ERROR_INVALID_ARGUMENT,
                          "application.configuration", "Application 创建参数无效");
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto state = std::make_shared<gneiss::application_internal::application_state>(normalized_desc);
    const auto initialize_result = state->initialize();
    if (initialize_result != GNEISS_SUCCESS) {
      return initialize_result;
    }
    auto& registry = get_application_registry();
    const std::scoped_lock lock{registry.mutex};
    return registry.applications.create(gneiss::core::resource_type::application, std::move(state),
                                        out_application);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_application_destroy(gneiss_application application) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    const auto shutdown_result = state->shutdown(application);
    auto& registry = get_application_registry();
    const std::scoped_lock lock{registry.mutex};
    const auto destroy_result =
        registry.applications.destroy(application, gneiss::core::resource_type::application);
    return shutdown_result == GNEISS_SUCCESS ? destroy_result : shutdown_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_application_run(gneiss_application application,
                                                uint64_t max_frame_count) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    const auto result = state->run(application, max_frame_count);
    if (result != GNEISS_SUCCESS) {
      state->report(application, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_BACKEND,
                    result, "application", "主循环因运行时错误终止");
    }
    return result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_application_request_exit(gneiss_application application) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    state->request_exit();
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_application_get_window_size(gneiss_application application,
                                                            uint32_t* out_width,
                                                            uint32_t* out_height) {
  if (out_width == nullptr || out_height == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_width = 0U;
  *out_height = 0U;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->get_window_size(*out_width, *out_height)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 参数具有不同语义和取值范围。
extern "C" gneiss_result gneiss_application_set_paused(gneiss_application application,
                                                       uint8_t is_paused) {
  if (is_paused > UINT8_C(1)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    state->set_paused(is_paused != 0U);
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result
gneiss::application_internal::submit_application_log(gneiss_application application,
                                                     const gneiss_log_message& message,
                                                     std::string_view source) noexcept {
  try {
    auto state = find_application(application);
    return state == nullptr ? GNEISS_ERROR_INVALID_HANDLE
                            : state->submit_log(application, message, source);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_application_log(gneiss_application application,
                                                const gneiss_log_message* message) {
  const auto message_result = gneiss_log_message_validate(message);
  if (message_result != GNEISS_SUCCESS) {
    return message_result;
  }
  return gneiss::application_internal::submit_application_log(application, *message, "application");
}

extern "C" gneiss_result gneiss_application_get_world(gneiss_application application,
                                                      gneiss_world* out_world) {
  if (out_world == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_world = GNEISS_NULL_WORLD;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    *out_world = state->world();
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_mesh_create(gneiss_application application,
                                            const gneiss_mesh_desc* desc, gneiss_mesh* out_mesh) {
  if (out_mesh == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_mesh = GNEISS_NULL_MESH;
  if (desc == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->resources().create_mesh(*desc, out_mesh)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 参数均为不透明句柄，名称区分语义。
extern "C" gneiss_result gneiss_mesh_destroy(gneiss_application application, gneiss_mesh mesh) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->resources().destroy_mesh(mesh)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_material_create(gneiss_application application,
                                                const gneiss_material_desc* desc,
                                                gneiss_material* out_material) {
  if (out_material == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_material = GNEISS_NULL_MATERIAL;
  if (desc == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->resources().create_material(*desc, out_material)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 参数均为不透明句柄，名称区分语义。
extern "C" gneiss_result gneiss_material_destroy(gneiss_application application,
                                                 gneiss_material material) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->resources().destroy_material(material)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_texture_create(gneiss_application application,
                                               const gneiss_texture_desc* desc,
                                               gneiss_texture* out_texture) {
  if (out_texture == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_texture = GNEISS_NULL_TEXTURE;
  if (desc == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->resources().create_texture(*desc, out_texture)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 参数均为不透明句柄，名称区分语义。
extern "C" gneiss_result gneiss_texture_destroy(gneiss_application application,
                                                gneiss_texture texture) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->resources().destroy_texture(texture)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result
gneiss_application_submit_ui_draw_list(gneiss_application application,
                                       const gneiss_ui_draw_list_desc* desc) {
  if (desc == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->submit_ui_draw_list(*desc)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result
gneiss_application_submit_debug_draw_list(gneiss_application application,
                                          const gneiss_debug_draw_list_desc* desc) {
  if (desc == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->submit_debug_draw_list(*desc)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_load(gneiss_application application, const char* uri,
                                                    uint64_t uri_length,
                                                    gneiss_scene_instance* out_instance) {
  if (out_instance == nullptr || uri == nullptr || uri_length == 0U ||
      uri_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_instance = GNEISS_NULL_SCENE_INSTANCE;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    const auto uri_view = std::string_view(uri, static_cast<std::size_t>(uri_length));
    const auto result = state->scenes()->load(uri_view, out_instance);
    if (result != GNEISS_SUCCESS) {
      auto message = std::string{"场景加载失败："};
      message.append(uri_view);
      state->report(application, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_ASSET, result,
                    "scene.load", message);
    }
    return result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_create_empty(gneiss_application application,
                                                            const char* scene_uuid,
                                                            uint64_t scene_uuid_length,
                                                            gneiss_scene_instance* out_instance) {
  if (out_instance == nullptr || scene_uuid == nullptr || scene_uuid_length == 0U ||
      scene_uuid_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_instance = GNEISS_NULL_SCENE_INSTANCE;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->create_empty(
                     std::string_view(scene_uuid, static_cast<std::size_t>(scene_uuid_length)),
                     out_instance)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 句柄名称区分所属关系。
extern "C" gneiss_result gneiss_scene_instance_unload(gneiss_application application,
                                                      gneiss_scene_instance instance) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->scenes()->unload(instance)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 句柄名称区分所属关系。
extern "C" gneiss_result gneiss_scene_instance_find_node(gneiss_application application,
                                                         gneiss_scene_instance instance,
                                                         const char* uuid, uint64_t uuid_length,
                                                         gneiss_scene_node_id* out_node) {
  if (out_node == nullptr || uuid == nullptr || uuid_length == 0U ||
      uuid_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_node = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    return state->scenes()->find_node(
        instance, std::string_view(uuid, static_cast<std::size_t>(uuid_length)), out_node);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_get_node_count(gneiss_application application,
                                                              gneiss_scene_instance instance,
                                                              uint64_t* out_count) {
  if (out_count == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_count = 0U;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->get_node_count(instance, out_count)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result
gneiss_scene_instance_get_node_info(gneiss_application application, gneiss_scene_instance instance,
                                    uint64_t index, gneiss_scene_instance_node_info* out_info) {
  if (out_info == nullptr ||
      out_info->struct_size < GNEISS_SCENE_INSTANCE_NODE_INFO_VERSION_1_SIZE) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->get_node_info(instance, index, out_info)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_get_prefab_node_count(gneiss_application application,
                                                                     gneiss_scene_instance instance,
                                                                     uint64_t* out_count) {
  if (out_count == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_count = 0U;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->get_prefab_node_count(instance, out_count)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result
gneiss_scene_instance_get_prefab_node_info(gneiss_application application,
                                           gneiss_scene_instance instance, uint64_t index,
                                           gneiss_scene_prefab_node_info* out_info) {
  if (out_info == nullptr || out_info->struct_size < sizeof(gneiss_scene_prefab_node_info)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->get_prefab_node_info(instance, index, out_info)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_create_prefab_instance(
    gneiss_application application, gneiss_scene_instance instance,
    const gneiss_scene_prefab_instance_desc* desc, gneiss_scene_node_id* out_root) {
  if (out_root == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_root = GNEISS_NULL_SCENE_NODE_ID;
  if (desc == nullptr || desc->struct_size < sizeof(gneiss_scene_prefab_instance_desc) ||
      desc->instance_uuid == nullptr || desc->instance_uuid_length == 0U ||
      (desc->name == nullptr && desc->name_length != 0U) || desc->prefab_uri == nullptr ||
      desc->prefab_uri_length == 0U ||
      desc->instance_uuid_length > std::numeric_limits<std::size_t>::max() ||
      desc->name_length > std::numeric_limits<std::size_t>::max() ||
      desc->prefab_uri_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->create_prefab_instance(instance, *desc, out_root)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_set_prefab_instance_name(
    gneiss_application application, gneiss_scene_instance instance, gneiss_scene_node_id root,
    const char* name, uint64_t name_length) {
  if ((name == nullptr && name_length != 0U) ||
      name_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->set_prefab_instance_name(
                     instance, root,
                     std::string_view{name == nullptr ? "" : name,
                                      static_cast<std::size_t>(name_length)})
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_destroy_prefab_instance(
    gneiss_application application, gneiss_scene_instance instance, gneiss_scene_node_id root) {
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->destroy_prefab_instance(instance, root)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_refresh_prefab_instance(
    gneiss_application application, gneiss_scene_instance instance, gneiss_scene_node_id root,
    gneiss_scene_node_id* out_new_root) {
  if (out_new_root == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_new_root = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->refresh_prefab_instance(instance, root, out_new_root)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_create_node(gneiss_application application,
                                                           gneiss_scene_instance instance,
                                                           const gneiss_scene_node_desc* desc,
                                                           gneiss_scene_node_id* out_node) {
  if (desc == nullptr || out_node == nullptr ||
      desc->struct_size < sizeof(gneiss_scene_node_desc) || desc->uuid == nullptr ||
      desc->uuid_length == 0U || (desc->name == nullptr && desc->name_length != 0U) ||
      desc->uuid_length > std::numeric_limits<std::size_t>::max() ||
      desc->name_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_node = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->create_node(instance, *desc, out_node)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_set_node_name(gneiss_application application,
                                                             gneiss_scene_instance instance,
                                                             gneiss_scene_node_id node,
                                                             const char* name,
                                                             uint64_t name_length) {
  if (node == GNEISS_NULL_SCENE_NODE_ID || (name == nullptr && name_length != 0U) ||
      name_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->set_node_name(
                     instance, node,
                     std::string_view(name == nullptr ? "" : name,
                                      static_cast<std::size_t>(name_length)))
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_reparent_node(gneiss_application application,
                                                             gneiss_scene_instance instance,
                                                             gneiss_scene_node_id node,
                                                             gneiss_scene_node_id parent) {
  if (node == GNEISS_NULL_SCENE_NODE_ID) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->reparent_node(instance, node, parent)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_capture_subtree(gneiss_application application,
                                                               gneiss_scene_instance instance,
                                                               gneiss_scene_node_id root,
                                                               char* buffer, uint64_t capacity,
                                                               uint64_t* out_length) {
  if (root == GNEISS_NULL_SCENE_NODE_ID || out_length == nullptr ||
      (buffer == nullptr && capacity != 0U) || capacity > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_length = 0U;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    std::string snapshot;
    const auto result = state->scenes()->capture_subtree(instance, root, snapshot);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    *out_length = snapshot.size();
    if (buffer == nullptr) {
      return capacity == 0U ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (capacity < snapshot.size()) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    std::ranges::copy(snapshot, buffer);
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result
gneiss_scene_instance_restore_subtree(gneiss_application application,
                                      gneiss_scene_instance instance, const char* snapshot,
                                      uint64_t snapshot_length, gneiss_scene_node_id parent,
                                      const gneiss_scene_uuid_mapping* mappings,
                                      uint64_t mapping_count, gneiss_scene_node_id* out_root) {
  if (snapshot == nullptr || snapshot_length == 0U || out_root == nullptr ||
      snapshot_length > std::numeric_limits<std::size_t>::max() ||
      mapping_count > std::numeric_limits<std::size_t>::max() ||
      (mappings == nullptr && mapping_count != 0U)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  for (std::uint64_t index = 0; index < mapping_count; ++index) {
    const auto& mapping = mappings[index];
    if (mapping.source_uuid == nullptr || mapping.target_uuid == nullptr ||
        mapping.source_uuid_length == 0U || mapping.target_uuid_length == 0U ||
        mapping.source_uuid_length > std::numeric_limits<std::size_t>::max() ||
        mapping.target_uuid_length > std::numeric_limits<std::size_t>::max()) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  *out_root = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->restore_subtree(
                     instance,
                     std::string_view(snapshot, static_cast<std::size_t>(snapshot_length)), parent,
                     mappings, mapping_count, out_root)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_destroy_subtree(gneiss_application application,
                                                               gneiss_scene_instance instance,
                                                               gneiss_scene_node_id root) {
  if (root == GNEISS_NULL_SCENE_NODE_ID) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->scenes()->destroy_subtree(instance, root)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_create_mesh_renderer_node(
    gneiss_application application, gneiss_scene_instance instance,
    const gneiss_scene_mesh_renderer_node_desc* desc, gneiss_scene_node_id* out_node) {
  if (desc == nullptr || out_node == nullptr ||
      desc->struct_size < sizeof(gneiss_scene_mesh_renderer_node_desc) ||
      desc->renderer.struct_size < sizeof(gneiss_scene_mesh_renderer_desc) ||
      desc->uuid == nullptr || desc->uuid_length == 0U ||
      (desc->name == nullptr && desc->name_length != 0U) || desc->renderer.mesh_uri == nullptr ||
      desc->renderer.mesh_uri_length == 0U || desc->renderer.material_uri == nullptr ||
      desc->renderer.material_uri_length == 0U ||
      desc->uuid_length > std::numeric_limits<std::size_t>::max() ||
      desc->name_length > std::numeric_limits<std::size_t>::max() ||
      desc->renderer.mesh_uri_length > std::numeric_limits<std::size_t>::max() ||
      desc->renderer.material_uri_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_node = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->create_mesh_renderer_node(instance, *desc, out_node)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result
gneiss_scene_instance_set_mesh_renderer(gneiss_application application,
                                        gneiss_scene_instance instance, gneiss_scene_node_id node,
                                        const gneiss_scene_mesh_renderer_desc* desc) {
  if (desc == nullptr || desc->struct_size < sizeof(gneiss_scene_mesh_renderer_desc) ||
      node == GNEISS_NULL_SCENE_NODE_ID || desc->mesh_uri == nullptr ||
      desc->mesh_uri_length == 0U || desc->material_uri == nullptr ||
      desc->material_uri_length == 0U ||
      desc->mesh_uri_length > std::numeric_limits<std::size_t>::max() ||
      desc->material_uri_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->set_mesh_renderer(
                     instance, node,
                     std::string_view(desc->mesh_uri,
                                      static_cast<std::size_t>(desc->mesh_uri_length)),
                     std::string_view(desc->material_uri,
                                      static_cast<std::size_t>(desc->material_uri_length)))
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_set_camera(gneiss_application application,
                                                          gneiss_scene_instance instance,
                                                          gneiss_scene_node_id node,
                                                          const gneiss_scene_camera_desc* desc) {
  if (node == GNEISS_NULL_SCENE_NODE_ID || desc == nullptr ||
      desc->struct_size < sizeof(gneiss_scene_camera_desc) ||
      desc->camera.struct_size < sizeof(gneiss_camera_desc)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->scenes()->set_camera(instance, node, *desc)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_remove_camera(gneiss_application application,
                                                             gneiss_scene_instance instance,
                                                             gneiss_scene_node_id node) {
  if (node == GNEISS_NULL_SCENE_NODE_ID) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->scenes()->remove_camera(instance, node)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_remove_mesh_renderer(gneiss_application application,
                                                                    gneiss_scene_instance instance,
                                                                    gneiss_scene_node_id node) {
  if (node == GNEISS_NULL_SCENE_NODE_ID) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS
               ? state->scenes()->remove_mesh_renderer(instance, node)
               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_scene_instance_destroy_node(gneiss_application application,
                                                            gneiss_scene_instance instance,
                                                            gneiss_scene_node_id node) {
  if (node == GNEISS_NULL_SCENE_NODE_ID) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    return validation_result == GNEISS_SUCCESS ? state->scenes()->destroy_node(instance, node)
                                               : validation_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): C ABI 句柄名称区分所属关系。
extern "C" gneiss_result gneiss_scene_instance_serialize(gneiss_application application,
                                                         gneiss_scene_instance instance,
                                                         char* buffer, uint64_t capacity,
                                                         uint64_t* out_length) {
  if (out_length == nullptr || (buffer == nullptr && capacity != 0U) ||
      (buffer != nullptr && capacity > std::numeric_limits<std::size_t>::max())) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_length = 0U;
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    std::string json;
    const auto result = state->scenes()->serialize(instance, json);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    *out_length = json.size();
    if (buffer == nullptr) {
      return capacity == 0U ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (capacity < json.size()) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    std::ranges::copy(json, buffer);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_application_poll_input(gneiss_application application,
                                                       gneiss_input_event* out_event) {
  if (out_event == nullptr || out_event->struct_size < GNEISS_INPUT_EVENT_VERSION_1_SIZE) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    return state->poll_input(*out_event);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_application_get_keyboard_state(gneiss_application application,
                                                               gneiss_keyboard_state* out_state) {
  if (out_state == nullptr || out_state->struct_size < GNEISS_KEYBOARD_STATE_VERSION_1_SIZE) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    *out_state = state->keyboard_state();
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_application_get_pointer_state(gneiss_application application,
                                                              gneiss_pointer_state* out_state) {
  if (out_state == nullptr || out_state->struct_size < GNEISS_POINTER_STATE_VERSION_1_SIZE) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation_result = validate_application(state);
    if (validation_result != GNEISS_SUCCESS) {
      return validation_result;
    }
    *out_state = state->pointer_state();
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_application_load_action_map(gneiss_application application,
                                                            const char* uri, uint64_t uri_length) {
  if (uri == nullptr || uri_length == 0U || uri_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation = validate_application(state);
    if (validation != GNEISS_SUCCESS) {
      return validation;
    }
    const auto result = state->load_action_map({uri, static_cast<std::size_t>(uri_length)});
    if (result != GNEISS_SUCCESS) {
      state->report(application, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_INPUT, result,
                    "input", "动作映射加载失败");
    }
    return result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_application_find_action(gneiss_application application,
                                                        const char* name, uint64_t name_length,
                                                        gneiss_action* out_action) {
  if (out_action == nullptr || name == nullptr || name_length == 0U ||
      name_length > std::numeric_limits<std::size_t>::max()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_action = GNEISS_NULL_ACTION;
  try {
    auto state = find_application(application);
    const auto validation = validate_application(state);
    return validation == GNEISS_SUCCESS
               ? state->find_action({name, static_cast<std::size_t>(name_length)}, *out_action)
               : validation;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

extern "C" gneiss_result gneiss_application_get_action_state(gneiss_application application,
                                                             gneiss_action action,
                                                             gneiss_action_state* out_state) {
  if (out_state == nullptr || out_state->struct_size < GNEISS_ACTION_STATE_VERSION_1_SIZE) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto state = find_application(application);
    const auto validation = validate_application(state);
    return validation == GNEISS_SUCCESS ? state->get_action_state(action, *out_state) : validation;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
