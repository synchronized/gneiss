// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/rid_table.hpp"

#include <cstdint>

namespace {

struct test_resource {
  std::int32_t value;
};

} // namespace

int main() {
  using gneiss::engine::core::resource_type;
  using table = gneiss::engine::core::rid_table<test_resource>;

  table first{1};
  table second{2};
  gneiss::engine::core::rid original = gneiss::engine::core::null_rid;
  if (first.create(resource_type::test_resource, test_resource{42}, &original) != GNEISS_SUCCESS ||
      original == gneiss::engine::core::null_rid) {
    return 1;
  }
  if (first.get(gneiss::engine::core::null_rid, resource_type::test_resource) != nullptr ||
      first.get(original, resource_type::mesh) != nullptr ||
      second.get(original, resource_type::test_resource) != nullptr) {
    return 2;
  }
  const auto* resource = first.get(original, resource_type::test_resource);
  if (resource == nullptr || resource->value != 42 || first.live_count() != 1U) {
    return 3;
  }
  if (first.destroy(original, resource_type::test_resource) != GNEISS_SUCCESS ||
      first.destroy(original, resource_type::test_resource) != GNEISS_ERROR_INVALID_HANDLE ||
      first.get(original, resource_type::test_resource) != nullptr || first.live_count() != 0U) {
    return 4;
  }

  gneiss::engine::core::rid replacement = gneiss::engine::core::null_rid;
  if (first.create(resource_type::test_resource, test_resource{7}, &replacement) !=
          GNEISS_SUCCESS ||
      replacement == original || first.get(original, resource_type::test_resource) != nullptr) {
    return 5;
  }
  if (first.create(resource_type::invalid, test_resource{}, &original) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      first.create(resource_type::test_resource, test_resource{}, nullptr) !=
          GNEISS_ERROR_INVALID_ARGUMENT) {
    return 6;
  }
  return 0;
}
