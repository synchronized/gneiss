// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include "engine/resource/cache/resource_cache.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

int main() try {
  {
    using namespace gneiss::engine;
    using gneiss::engine::result;
    resource_cache index;
    auto source_a = std::make_shared<file_source_domain>();
    auto source_b = std::make_shared<file_source_domain>();
    asset_ref<const resource_cache::entry> a, b, c, again;
    const auto integer = [](auto& output) {
      output = std::make_shared<const int>(42);
      return result::success;
    };
    const auto real = [](auto& output) {
      output = std::make_shared<const float>(3.0F);
      return result::success;
    };
    if (index.load("asset://same", gneiss::engine::runtime_type_of<int>(), integer, a, source_a) !=
            result::success ||
        index.load("asset://same", gneiss::engine::runtime_type_of<float>(), real, b, source_a) !=
            result::success ||
        index.load("asset://same", gneiss::engine::runtime_type_of<int>(), integer, c, source_b) !=
            result::success ||
        index.load("asset://same", gneiss::engine::runtime_type_of<int>(), integer, again,
                   source_a) != result::success ||
        index.size() != 3U || a != again || a == b || a == c ||
        index.observe("asset://same", gneiss::engine::runtime_type_of<int>(), source_a)
                .lock()
                .get() != a.get())
      return 7;
    auto held = a.alias(static_cast<const int*>(a->resource.get()));
    a.reset();
    b.reset();
    c.reset();
    again.reset();
    index.release_unused();
    if (index.size() != 1U || *held != 42)
      return 7;
    held.reset();
    index.release_unused();
    if (index.size() != 0U)
      return 7;
  }

  gneiss::engine::resource_cache cache;
  std::size_t load_count = 0;
  auto loader = [&load_count](std::shared_ptr<const void>& resource) {
    ++load_count;
    resource = std::make_shared<int>(42);
    return gneiss::engine::result::success;
  };
  gneiss::engine::asset_ref<const gneiss::engine::resource_cache::entry> first;
  gneiss::engine::asset_ref<const gneiss::engine::resource_cache::entry> second;
  if (cache.load("asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(), loader,
                 first) != gneiss::engine::result::success ||
      cache.load("asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(), loader,
                 second) != gneiss::engine::result::success ||
      load_count != 1U || first != second || cache.size() != 1U) {
    return 3;
  }
  auto reloader = [&load_count](std::shared_ptr<const void>& resource) {
    ++load_count;
    resource = std::make_shared<int>(84);
    return gneiss::engine::result::success;
  };
  if (cache.load("asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(), reloader,
                 second, {}, true) != gneiss::engine::result::success ||
      load_count != 2U || first == second ||
      *static_cast<const int*>(first->resource.get()) != 42 ||
      *static_cast<const int*>(second->resource.get()) != 84 || cache.size() != 1U) {
    return 3;
  }
  gneiss::engine::asset_ref<const gneiss::engine::resource_cache::entry> failed_reload;
  const auto failed_reloader = [](std::shared_ptr<const void>&) {
    return gneiss::engine::result::io;
  };
  if (cache.load("asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(),
                 failed_reloader, failed_reload, {}, true) != gneiss::engine::result::io ||
      failed_reload ||
      cache.load("asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(), loader,
                 failed_reload) != gneiss::engine::result::success ||
      failed_reload != second || load_count != 2U) {
    return 3;
  }
  // 抛异常与空结果都不能污染索引，重载失败仍可获取旧值。
  const auto throwing = [](std::shared_ptr<const void>&) -> gneiss::engine::result {
    throw std::bad_alloc{};
  };
  if (cache.load("asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(), throwing,
                 failed_reload, {}, true) != gneiss::engine::result::out_of_memory ||
      cache.load("asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(), loader,
                 failed_reload) != gneiss::engine::result::success ||
      failed_reload != second)
    return 8;
  if (cache.load("asset://throws", gneiss::engine::runtime_type_of<int>(), throwing, first) !=
          gneiss::engine::result::out_of_memory ||
      cache.size() != 1U)
    return 8;
  const auto empty = [](std::shared_ptr<const void>&) { return gneiss::engine::result::success; };
  if (cache.load("asset://empty", gneiss::engine::runtime_type_of<int>(), empty, first) !=
          gneiss::engine::result::internal ||
      cache.size() != 1U)
    return 8;
  const auto recursive = [&](std::shared_ptr<const void>& output) {
    decltype(first) nested;
    if (cache.load("asset://recursive", gneiss::engine::runtime_type_of<int>(), loader, nested) !=
        gneiss::engine::result::invalid_state)
      return gneiss::engine::result::internal;
    cache.release_unused(); // 加载中的条目必须由当前调用保活。
    output = std::make_shared<int>(7);
    return gneiss::engine::result::success;
  };
  if (cache.load("asset://recursive", gneiss::engine::runtime_type_of<int>(), recursive, first) !=
          gneiss::engine::result::success ||
      cache.size() != 2U)
    return 8;
  first.reset();
  second.reset();
  failed_reload.reset();
  cache.release_unused();
  if (cache.size() != 0U) {
    return 4;
  }

  std::size_t failure_count = 0;
  auto failing_loader = [&failure_count](std::shared_ptr<const void>&) {
    ++failure_count;
    return gneiss::engine::result::io;
  };
  if (cache.load("asset://models/broken.mesh", gneiss::engine::runtime_type_of<int>(),
                 failing_loader, first) != gneiss::engine::result::io ||
      cache.load("asset://models/broken.mesh", gneiss::engine::runtime_type_of<int>(),
                 failing_loader, first) != gneiss::engine::result::io ||
      failure_count != 2U || cache.size() != 0U) {
    return 5;
  }

  gneiss::engine::resource_cache other_cache;
  if (other_cache.load("asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(),
                       loader, first) != gneiss::engine::result::success ||
      load_count != 3U) {
    return 6;
  }
  return 0;
} catch (...) {
  return 99;
}
