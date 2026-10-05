// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/application_registry.hpp"

#include "application/application_state.hpp"
#include "core/rid_table.h"
#include <mutex>
#include <new>
#include <utility>

namespace gneiss::application_internal {
namespace {
using application_table = gneiss::core::rid_table<application_resource>;

struct application_registry {
  std::mutex mutex;
  application_table applications{2};
};

application_registry& get_application_registry() {
  static application_registry registry;
  return registry;
}

} // namespace

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

gneiss_result create_application(const gneiss_application_desc& normalized_desc,
                                 gneiss_application& output) noexcept {
  output = GNEISS_NULL_APPLICATION;
  try {
    auto state = std::make_shared<gneiss::application_internal::application_state>(normalized_desc);
    const auto initialize_result = state->initialize();
    if (initialize_result != GNEISS_SUCCESS) {
      return initialize_result;
    }
    auto& registry = get_application_registry();
    const std::scoped_lock lock{registry.mutex};
    return registry.applications.create(gneiss::core::resource_type::application, std::move(state),
                                        &output);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result destroy_application(gneiss_application application) noexcept {
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

} // namespace gneiss::application_internal
