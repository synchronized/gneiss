// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/application_registry.hpp"
#include "engine/core/result.hpp"

#include "engine/core/rid_table.hpp"
#include "runtime/application.hpp"
#include <mutex>
#include <new>
#include <utility>

namespace gneiss::runtime {
namespace {
struct application_entry {
  application_resource owner;
  std::weak_ptr<runtime::application> borrowed;
  std::thread::id thread;
};
using application_table = gneiss::engine::rid_table<application_entry>;

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
      registry.applications.get(application, gneiss::engine::resource_type::application);
  if (resource == nullptr) {
    return nullptr;
  }
  if (resource->owner) {
    return resource->owner;
  }
  auto borrowed = resource->borrowed.lock();
  return borrowed && borrowed->accepts_requests() ? borrowed : nullptr;
}

gneiss::engine::result validate_application(const application_resource& application) noexcept {
  if (application == nullptr) {
    return gneiss::engine::result::invalid_handle;
  }
  return application->is_owner_thread() ? gneiss::engine::result::success
                                        : gneiss::engine::result::invalid_state;
}

gneiss::engine::result create_application(const application_configuration& config,
                                          gneiss_application& output) noexcept {
  output = GNEISS_NULL_APPLICATION;
  try {
    application_resource state;
    const auto initialize_result = runtime::application::create(config, state);
    if (initialize_result != gneiss::engine::result::success) {
      return initialize_result;
    }
    auto& registry = get_application_registry();
    const std::scoped_lock lock{registry.mutex};
    return registry.applications.create(gneiss::engine::resource_type::application,
                                        application_entry{.owner = std::move(state),
                                                          .borrowed = {},
                                                          .thread = std::this_thread::get_id()},
                                        &output);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result destroy_application(gneiss_application application) noexcept {
  try {
    application_resource state;
    auto& registry = get_application_registry();
    {
      const std::scoped_lock lock{registry.mutex};
      const auto* entry =
          registry.applications.get(application, gneiss::engine::resource_type::application);
      if (entry == nullptr) {
        return gneiss::engine::result::invalid_handle;
      }
      if (!entry->owner) {
        return gneiss::engine::result::invalid_state;
      }
      state = entry->owner;
    }
    const auto validation_result = validate_application(state);
    if (validation_result != gneiss::engine::result::success) {
      return validation_result;
    }
    const auto shutdown_result = state->shutdown(application);
    if (shutdown_result == gneiss::engine::result::invalid_state) {
      return shutdown_result;
    }
    const std::scoped_lock lock{registry.mutex};
    const auto destroy_result =
        registry.applications.destroy(application, gneiss::engine::resource_type::application);
    return shutdown_result == gneiss::engine::result::success ? destroy_result : shutdown_result;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result application_bridge::bind(const application_resource& application) noexcept {
  if (handle_ != GNEISS_NULL_APPLICATION) {
    return gneiss::engine::result::invalid_state;
  }
  const auto validation = validate_application(application);
  if (validation != gneiss::engine::result::success) {
    return validation;
  }
  if (!application->accepts_requests()) {
    return gneiss::engine::result::invalid_state;
  }
  auto& registry = get_application_registry();
  const std::scoped_lock lock{registry.mutex};
  return registry.applications.create(
      gneiss::engine::resource_type::application,
      application_entry{.owner = {}, .borrowed = application, .thread = std::this_thread::get_id()},
      &handle_);
}

gneiss::engine::result application_bridge::reset() noexcept {
  if (handle_ == GNEISS_NULL_APPLICATION) {
    return gneiss::engine::result::success;
  }
  auto& registry = get_application_registry();
  const std::scoped_lock lock{registry.mutex};
  const auto* entry =
      registry.applications.get(handle_, gneiss::engine::resource_type::application);
  if (entry == nullptr || entry->owner) {
    return gneiss::engine::result::invalid_handle;
  }
  if (entry->thread != std::this_thread::get_id()) {
    return gneiss::engine::result::invalid_state;
  }
  const auto result =
      registry.applications.destroy(handle_, gneiss::engine::resource_type::application);
  if (result == gneiss::engine::result::success) {
    handle_ = GNEISS_NULL_APPLICATION;
  }
  return result;
}

} // namespace gneiss::runtime
