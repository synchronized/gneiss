// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/application_state.h"
#include "core/rid_table.h"

#include <gneiss/application.h>
#include <gneiss/input.h>
#include <gneiss/scene.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>

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
    auto& registry = get_application_registry();
    const std::scoped_lock lock{registry.mutex};
    return registry.applications.destroy(application, gneiss::core::resource_type::application);
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
    return state->scenes()->load(std::string_view(uri, static_cast<std::size_t>(uri_length)),
                                 out_instance);
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
