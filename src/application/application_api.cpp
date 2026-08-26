// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/application_state.h"
#include "core/rid_table.h"

#include <gneiss/application.h>

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
  if (desc == nullptr || desc->struct_size < sizeof(gneiss_application_desc) ||
      desc->reserved != 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto state = std::make_shared<gneiss::application_internal::application_state>(*desc);
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
    return validation_result == GNEISS_SUCCESS ? state->run(application, max_frame_count)
                                               : validation_result;
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
