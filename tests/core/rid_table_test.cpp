// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "core/rid_table.h"

#include <cstdint>

namespace {

struct test_resource {
  std::int32_t value;
};

} // namespace

int main() {
  using gneiss::core::resource_type;
  using table = gneiss::core::rid_table<test_resource>;

  table first{1};
  table second{2};
  gneiss_rid original = GNEISS_NULL_RID;
  if (first.create(resource_type::test_resource, test_resource{42}, &original) != GNEISS_SUCCESS ||
      original == GNEISS_NULL_RID) {
    return 1;
  }
  if (first.get(GNEISS_NULL_RID, resource_type::test_resource) != nullptr ||
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

  gneiss_rid replacement = GNEISS_NULL_RID;
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
