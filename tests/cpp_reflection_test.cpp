// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/reflection.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr gneiss::type_id camera_type{{0x30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};
constexpr gneiss::type_id float_type{{0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2}};

bool verify_registry_queries(const gneiss::type_registry& registry) {
  bool frozen{};
  gneiss::type_info info{};
  if (registry.is_frozen(frozen) != gneiss::result::success || !frozen ||
      registry.type_at(0U, info) != gneiss::result::success || info.name != "Camera" ||
      registry.type_at(1U, info) != gneiss::result::not_found) {
    return false;
  }
  gneiss::type_registry empty;
  return empty.is_frozen(frozen) == gneiss::result::invalid_handle && frozen &&
         empty.type_at(0U, info) == gneiss::result::invalid_handle;
}

int run_tests() {
  gneiss::type_registry registry;
  if (gneiss::type_registry::create(registry) != gneiss::result::success || !registry) {
    return 1;
  }

  bool frozen = true;
  gneiss::type_info early{};
  if (registry.is_frozen(frozen) != gneiss::result::success || frozen ||
      registry.type_at(0U, early) != gneiss::result::not_ready) {
    return 20;
  }
  static constexpr std::array fields{
      gneiss::field_desc{
          .id = gneiss::field_id{2U},
          .value_type_id = float_type,
          .flags = gneiss::field_flags::read_only,
          .name = "far_plane",
      },
      gneiss::field_desc{
          .id = gneiss::field_id{1U},
          .value_type_id = float_type,
          .flags = gneiss::field_flags::none,
          .name = "field_of_view",
      },
  };
  static constexpr gneiss::type_desc type{
      .id = camera_type,
      .schema_version = 1U,
      .name = "Camera",
      .fields = fields,
  };
  if (registry.register_type(type) != gneiss::result::success ||
      registry.freeze() != gneiss::result::success) {
    return 2;
  }

  std::uint32_t count = 0;
  gneiss::type_info info{};
  gneiss::field_info field{};
  if (registry.type_count(count) != gneiss::result::success || count != 1U ||
      registry.find_type(camera_type, info) != gneiss::result::success ||
      info.fields.size() != 2U || info.fields[0].id != gneiss::field_id{1} ||
      info.fields[1].id != gneiss::field_id{2} ||
      registry.find_field(camera_type, gneiss::field_id{2}, field) != gneiss::result::success ||
      field.flags != gneiss::field_flags::read_only || field.name != "far_plane") {
    return 3;
  }

  if (!verify_registry_queries(registry)) {
    return 21;
  }
  std::atomic<bool> queries_succeeded = true;
  std::vector<std::thread> workers;
  workers.reserve(8U);
  for (std::uint32_t worker = 0; worker < 8U; ++worker) {
    workers.emplace_back([&registry, &queries_succeeded] {
      for (std::uint32_t iteration = 0; iteration < 1000U; ++iteration) {
        gneiss::type_info queried{};
        if (registry.find_type(camera_type, queried) != gneiss::result::success ||
            queried.fields.size() != 2U) {
          queries_succeeded = false;
          return;
        }
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  if (!queries_succeeded) {
    return 4;
  }

  gneiss::type_registry isolated;
  gneiss::type_info isolated_info{};
  if (gneiss::type_registry::create(isolated) != gneiss::result::success ||
      isolated.freeze() != gneiss::result::success ||
      isolated.find_type(camera_type, isolated_info) != gneiss::result::not_found) {
    return 5;
  }

  gneiss::type_registry moved = std::move(registry);
  if (!moved) {
    return 6;
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
