// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/reflection.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr gneiss_type_id camera_type{{0x30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};
constexpr gneiss_type_id float_type{{0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2}};

int run_tests() {
  gneiss::type_registry registry;
  if (gneiss::type_registry::create(registry) != gneiss::result::success || !registry) {
    return 1;
  }

  static constexpr std::array fields{
      gneiss_field_desc{.struct_size = sizeof(gneiss_field_desc),
                        .id = 2U,
                        .value_type_id = float_type,
                        .flags = GNEISS_FIELD_FLAG_READ_ONLY,
                        .name = "far_plane",
                        .name_length = 9U},
      gneiss_field_desc{.struct_size = sizeof(gneiss_field_desc),
                        .id = 1U,
                        .value_type_id = float_type,
                        .flags = 0U,
                        .name = "field_of_view",
                        .name_length = 13U},
  };
  static constexpr gneiss_type_desc type{.struct_size = sizeof(gneiss_type_desc),
                                         .id = camera_type,
                                         .schema_version = 1U,
                                         .name = "Camera",
                                         .name_length = 6U,
                                         .fields = fields.data(),
                                         .field_count = static_cast<std::uint32_t>(fields.size())};
  if (registry.register_type(type) != gneiss::result::success ||
      registry.freeze() != gneiss::result::success) {
    return 2;
  }

  std::uint32_t count = 0;
  gneiss_type_info info{};
  gneiss_field_info field{};
  if (registry.type_count(count) != gneiss::result::success || count != 1U ||
      registry.find_type(camera_type, info) != gneiss::result::success || info.field_count != 2U ||
      info.fields[0].id != 1U || info.fields[1].id != 2U ||
      registry.find_field(camera_type, 2U, field) != gneiss::result::success ||
      field.flags != GNEISS_FIELD_FLAG_READ_ONLY ||
      std::string_view(field.name, field.name_length) != "far_plane") {
    return 3;
  }

  std::atomic<bool> queries_succeeded = true;
  std::vector<std::thread> workers;
  workers.reserve(8U);
  for (std::uint32_t worker = 0; worker < 8U; ++worker) {
    workers.emplace_back([&registry, &queries_succeeded] {
      for (std::uint32_t iteration = 0; iteration < 1000U; ++iteration) {
        gneiss_type_info queried{};
        if (registry.find_type(camera_type, queried) != gneiss::result::success ||
            queried.field_count != 2U) {
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
  gneiss_type_info isolated_info{};
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
