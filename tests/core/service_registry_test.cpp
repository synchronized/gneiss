// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "core/service_registry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <span>

namespace {

using gneiss::core::service_kind;

class event_log final {
public:
  void push(int value) noexcept { events_[size_++] = value; }

  [[nodiscard]] bool matches(std::initializer_list<int> expected) const noexcept {
    return expected.size() == size_ &&
           std::equal(expected.begin(), expected.end(), events_.begin());
  }

  [[nodiscard]] bool is_empty() const noexcept { return size_ == 0U; }

private:
  std::array<int, 16> events_{};
  std::size_t size_ = 0;
};

struct test_service_config {
  service_kind kind;
  std::span<const service_kind> dependencies;
  int event_id;
  gneiss_result initialize_result;
  std::size_t live_resources = 0;
};

class test_service final : public gneiss::core::service {
public:
  test_service(test_service_config config, event_log& events) noexcept
      : config_(config), events_(events) {}

  [[nodiscard]] service_kind kind() const noexcept override { return config_.kind; }
  [[nodiscard]] std::span<const service_kind> dependencies() const noexcept override {
    return config_.dependencies;
  }
  [[nodiscard]] gneiss_result initialize() noexcept override {
    events_.push(config_.event_id);
    return config_.initialize_result;
  }
  void shutdown() noexcept override { events_.push(-config_.event_id); }
  [[nodiscard]] std::size_t live_resource_count() const noexcept override {
    return config_.live_resources;
  }

private:
  test_service_config config_;
  event_log& events_;
};

int run_tests() {
  const std::array platform_dependency{service_kind::platform};
  const std::array render_dependencies{service_kind::platform, service_kind::resource};
  event_log events;

  gneiss::core::service_registry registry;
  if (registry.add(
          std::make_unique<test_service>(test_service_config{.kind = service_kind::render,
                                                             .dependencies = render_dependencies,
                                                             .event_id = 3,
                                                             .initialize_result = GNEISS_SUCCESS,
                                                             .live_resources = 2},
                                         events)) != GNEISS_SUCCESS ||
      registry.add(
          std::make_unique<test_service>(test_service_config{.kind = service_kind::resource,
                                                             .dependencies = platform_dependency,
                                                             .event_id = 2,
                                                             .initialize_result = GNEISS_SUCCESS,
                                                             .live_resources = 1},
                                         events)) != GNEISS_SUCCESS ||
      registry.add(
          std::make_unique<test_service>(test_service_config{.kind = service_kind::platform,
                                                             .dependencies = {},
                                                             .event_id = 1,
                                                             .initialize_result = GNEISS_SUCCESS},
                                         events)) != GNEISS_SUCCESS) {
    return 1;
  }
  if (registry.initialize() != GNEISS_SUCCESS || !events.matches({1, 2, 3})) {
    return 2;
  }
  if (!registry.is_initialized(service_kind::render) ||
      registry.initialize() != GNEISS_ERROR_INVALID_STATE || registry.shutdown() != 3U ||
      !events.matches({1, 2, 3, -3, -2, -1})) {
    return 3;
  }

  event_log rollback_events;
  gneiss::core::service_registry rollback_registry;
  if (rollback_registry.add(
          std::make_unique<test_service>(test_service_config{.kind = service_kind::platform,
                                                             .dependencies = {},
                                                             .event_id = 1,
                                                             .initialize_result = GNEISS_SUCCESS},
                                         rollback_events)) != GNEISS_SUCCESS ||
      rollback_registry.add(std::make_unique<test_service>(
          test_service_config{.kind = service_kind::resource,
                              .dependencies = platform_dependency,
                              .event_id = 2,
                              .initialize_result = GNEISS_ERROR_INITIALIZATION_FAILED},
          rollback_events)) != GNEISS_SUCCESS ||
      rollback_registry.initialize() != GNEISS_ERROR_INITIALIZATION_FAILED ||
      !rollback_events.matches({1, 2, -2, -1})) {
    return 4;
  }

  event_log missing_events;
  gneiss::core::service_registry missing_registry;
  if (missing_registry.add(
          std::make_unique<test_service>(test_service_config{.kind = service_kind::render,
                                                             .dependencies = render_dependencies,
                                                             .event_id = 3,
                                                             .initialize_result = GNEISS_SUCCESS},
                                         missing_events)) != GNEISS_SUCCESS ||
      missing_registry.initialize() != GNEISS_ERROR_DEPENDENCY_FAILED ||
      !missing_events.is_empty()) {
    return 5;
  }
  return 0;
}

} // namespace

int main() {
  try {
    return run_tests();
  } catch (...) {
    return 99;
  }
}
