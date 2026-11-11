// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/core/service_registry.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <span>

namespace {

using gneiss::service_kind;

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
  gneiss::error initialize_result;
  std::size_t live_resources = 0;
  bool throw_initialize = false;
};

class test_service final : public gneiss::service {
public:
  test_service(test_service_config config, event_log& events) noexcept
      : config_(config), events_(events) {}

  [[nodiscard]] service_kind kind() const noexcept override { return config_.kind; }
  [[nodiscard]] std::span<const service_kind> dependencies() const noexcept override {
    return config_.dependencies;
  }
  [[nodiscard]] gneiss::error initialize() override {
    events_.push(config_.event_id);
    if (config_.throw_initialize)
      throw 42;
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
  {
    event_log events;
    gneiss::service_registry registry;
    if (registry
            .add(std::make_unique<test_service>(
                test_service_config{.kind = service_kind::platform,
                                    .dependencies = {},
                                    .event_id = 1,
                                    .initialize_result = gneiss::error::success,
                                    .throw_initialize = true},
                events))
            .failed())
      return 20;
    bool propagated = false;
    try {
      (void)registry.initialize();
    } catch (int value) {
      propagated = value == 42;
    }
    if (!propagated || !events.matches({1, -1}) || registry.is_initialized(service_kind::platform))
      return 21;
  }

  const std::array platform_dependency{service_kind::platform};
  const std::array render_dependencies{service_kind::platform, service_kind::resource};
  event_log events;

  gneiss::service_registry registry;
  if (registry.add(std::make_unique<test_service>(
          test_service_config{.kind = service_kind::render,
                              .dependencies = render_dependencies,
                              .event_id = 3,
                              .initialize_result = gneiss::error::success,
                              .live_resources = 2},
          events)) != gneiss::error::success ||
      registry.add(std::make_unique<test_service>(
          test_service_config{.kind = service_kind::resource,
                              .dependencies = platform_dependency,
                              .event_id = 2,
                              .initialize_result = gneiss::error::success,
                              .live_resources = 1},
          events)) != gneiss::error::success ||
      registry.add(std::make_unique<test_service>(
          test_service_config{.kind = service_kind::platform,
                              .dependencies = {},
                              .event_id = 1,
                              .initialize_result = gneiss::error::success},
          events)) != gneiss::error::success) {
    return 1;
  }
  if (registry.initialize() != gneiss::error::success || !events.matches({1, 2, 3})) {
    return 2;
  }
  if (!registry.is_initialized(service_kind::render) ||
      registry.initialize() != gneiss::error::invalid_state || registry.shutdown() != 3U ||
      !events.matches({1, 2, 3, -3, -2, -1})) {
    return 3;
  }

  event_log rollback_events;
  gneiss::service_registry rollback_registry;
  if (rollback_registry.add(std::make_unique<test_service>(
          test_service_config{.kind = service_kind::platform,
                              .dependencies = {},
                              .event_id = 1,
                              .initialize_result = gneiss::error::success},
          rollback_events)) != gneiss::error::success ||
      rollback_registry.add(std::make_unique<test_service>(
          test_service_config{.kind = service_kind::resource,
                              .dependencies = platform_dependency,
                              .event_id = 2,
                              .initialize_result = gneiss::error::initialization_failed},
          rollback_events)) != gneiss::error::success ||
      rollback_registry.initialize() != gneiss::error::initialization_failed ||
      !rollback_events.matches({1, 2, -2, -1})) {
    return 4;
  }

  event_log missing_events;
  gneiss::service_registry missing_registry;
  if (missing_registry.add(std::make_unique<test_service>(
          test_service_config{.kind = service_kind::render,
                              .dependencies = render_dependencies,
                              .event_id = 3,
                              .initialize_result = gneiss::error::success},
          missing_events)) != gneiss::error::success ||
      missing_registry.initialize() != gneiss::error::dependency_failed ||
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
