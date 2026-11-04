// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/core/rid_table.hpp"

#include <cstdint>
#include <stdexcept>

namespace {

struct test_resource {
  std::int32_t value;
};

struct fallible_resource {
  explicit fallible_resource(int value) {
    if (value == 0)
      throw std::runtime_error("构造失败");
  }
};
} // namespace

int main() {
  using gneiss::engine::resource_type;
  using table = gneiss::engine::rid_table<test_resource>;

  table first{1};
  table second{2};
  auto created = first.create(resource_type::test_resource, test_resource{42});
  if (!created || *created == gneiss::engine::null_rid)
    return 1;
  const auto original = *created;
  if (first.get(gneiss::engine::null_rid, resource_type::test_resource) != nullptr ||
      first.get(original, resource_type::mesh) != nullptr ||
      second.get(original, resource_type::test_resource) != nullptr) {
    return 2;
  }
  const auto* resource = first.get(original, resource_type::test_resource);
  if (resource == nullptr || resource->value != 42 || first.live_count() != 1U) {
    return 3;
  }
  if (first.destroy(original, resource_type::test_resource) != gneiss::engine::error::success ||
      first.destroy(original, resource_type::test_resource) !=
          gneiss::engine::error::invalid_handle ||
      first.get(original, resource_type::test_resource) != nullptr || first.live_count() != 0U) {
    return 4;
  }

  auto replacement = first.create(resource_type::test_resource, test_resource{7});
  if (!replacement || *replacement == original || first.get(original, resource_type::test_resource))
    return 5;
  auto invalid = first.create(resource_type::invalid, test_resource{});
  if (invalid || invalid.error() != gneiss::engine::error::invalid_argument)
    return 6;
  first.close();
  auto closed = first.create(resource_type::test_resource, test_resource{});
  if (closed || closed.error() != gneiss::engine::error::invalid_argument)
    return 7;
  gneiss::engine::rid_table<fallible_resource> fallible{3};
  auto failed = fallible.create(resource_type::test_resource, 0);
  if (failed || failed.error() != gneiss::engine::error::internal || fallible.live_count() != 0)
    return 8;
  auto recovered = fallible.create(resource_type::test_resource, 1);
  if (!recovered || fallible.live_count() != 1)
    return 9;
  return 0;
}
