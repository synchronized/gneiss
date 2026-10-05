// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "reflection/type_registry.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
gneiss_result throwing_getter(void* /*context*/, gneiss_property_target /*target*/,
                              gneiss_property_value* /*output*/) {
  throw std::runtime_error("test callback failure");
}
} // namespace

int main() try {
  namespace reflection = gneiss::reflection_internal;
  gneiss_type_registry registry{};
  if (gneiss_type_registry_create(&registry) != GNEISS_SUCCESS) {
    return 1;
  }
  auto state = reflection::resolve(registry);
  const gneiss_type_id type_id{{1}};
  std::string name = "sample";
  const std::array fields{
      reflection::field_definition{
          .id = 1U,
          .value_type_id = type_id,
          .flags = 0U,
          .name = "value",
      },
  };
  const reflection::type_definition definition{
      .id = type_id,
      .schema_version = 1U,
      .name = name,
      .fields = fields,
  };
  if (reflection::register_type(state, definition) != GNEISS_SUCCESS ||
      reflection::bind_property(
          state, type_id, 1U, {.kind = GNEISS_PROPERTY_KIND_FLOAT32, .getter = throwing_getter}) !=
          GNEISS_SUCCESS) {
    return 2;
  }
  name.assign("changed");
  if (gneiss_type_registry_freeze(registry) != GNEISS_SUCCESS) {
    return 3;
  }
  gneiss_type_info info = GNEISS_TYPE_INFO_INIT;
  if (gneiss_type_registry_find_type(registry, type_id, &info) != GNEISS_SUCCESS ||
      std::string_view(info.name, info.name_length) != "sample" || info.field_count != 1U) {
    return 4;
  }
  gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
  if (gneiss_type_registry_get_property(registry, type_id, 1U, {}, &value) !=
          GNEISS_ERROR_INTERNAL ||
      value.kind != GNEISS_PROPERTY_KIND_INVALID) {
    return 5;
  }
  gneiss_result query{};
  std::uint32_t count{};
  std::thread worker([&] { query = gneiss_type_registry_type_count(registry, &count); });
  worker.join();
  if (query != GNEISS_SUCCESS || count != 1U) {
    return 6;
  }
  // 公共句柄失效与内部调用持有的临时租约是两个不同的生命周期。
  const auto stale = registry;
  if (reflection::destroy(registry) != GNEISS_SUCCESS ||
      gneiss_type_registry_type_count(stale, &count) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_type_registry_register(stale, nullptr) != GNEISS_ERROR_INVALID_HANDLE) {
    return 7;
  }
  state.reset();
  if (reflection::create(registry) != GNEISS_SUCCESS || registry == stale ||
      gneiss_type_registry_register(registry, nullptr) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 8;
  }
  return gneiss_type_registry_destroy(registry) == GNEISS_SUCCESS ? 0 : 9;
} catch (...) {
  return 10;
}
