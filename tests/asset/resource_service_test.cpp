// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/core/type_id.hpp"
#include "engine/function/resource/resource_cache.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

template <class Loader>
gneiss::engine::error load_cached(
    gneiss::engine::resource_cache& cache, std::string_view uri, gneiss::engine::type_id type,
    Loader loader, gneiss::engine::asset_ref<const gneiss::engine::resource_cache::entry>& output,
    std::shared_ptr<const gneiss::engine::files::source_domain> source = {}, bool reload = false) {
  auto loaded = cache.load(
      uri, type,
      [&]() -> gneiss::engine::result<std::shared_ptr<const void>> {
        std::shared_ptr<const void> value;
        auto status = loader(value);
        if (status.failed())
          return status;
        return value;
      },
      std::move(source), reload);
  if (!loaded) {
    output = {};
    return loaded.error();
  }
  output = std::move(*loaded);
  return gneiss::engine::error::success;
}
int main() try {
  {
    using namespace gneiss::engine;
    resource_cache cache;
    int calls = 0;
    const auto decode = [&]() -> result<std::shared_ptr<const void>> {
      ++calls;
      return std::shared_ptr<const void>(std::make_shared<const int>(42));
    };
    auto loaded = cache.load("asset://direct", runtime_type_of<int>(), decode);
    if (!loaded || *(*loaded)->as<int>() != 42)
      return 10;
    auto failed = cache.load(
        "asset://direct", runtime_type_of<int>(),
        []() -> result<std::shared_ptr<const void>> { return error::io; }, {}, true);
    if (failed || failed.error() != error::io)
      return 11;
    auto cached = cache.load("asset://direct", runtime_type_of<int>(), decode);
    if (!cached || *cached != *loaded || calls != 1)
      return 12;
  }
  {
    using namespace gneiss::engine;
    using gneiss::engine::error;
    resource_cache index;
    auto source_a = std::make_shared<gneiss::engine::files::source_domain>();
    auto source_b = std::make_shared<gneiss::engine::files::source_domain>();
    asset_ref<const resource_cache::entry> a, b, c, again;
    const auto integer = [](auto& output) {
      output = std::make_shared<const int>(42);
      return error::success;
    };
    const auto real = [](auto& output) {
      output = std::make_shared<const float>(3.0F);
      return error::success;
    };
    if (load_cached(index, "asset://same", gneiss::engine::runtime_type_of<int>(), integer, a,
                    source_a) != error::success ||
        load_cached(index, "asset://same", gneiss::engine::runtime_type_of<float>(), real, b,
                    source_a) != error::success ||
        load_cached(index, "asset://same", gneiss::engine::runtime_type_of<int>(), integer, c,
                    source_b) != error::success ||
        load_cached(index, "asset://same", gneiss::engine::runtime_type_of<int>(), integer, again,
                    source_a) != error::success ||
        index.size() != 3U || a != again || a == b || a == c ||
        index.observe("asset://same", gneiss::engine::runtime_type_of<int>(), source_a)
                .lock()
                .get() != a.get())
      return 7;
    auto held = a->as<int>();
    a.reset();
    b.reset();
    c.reset();
    again.reset();
    index.release_unused();
    if (index.size() != 1U || *held != 42)
      return 7;
    held.reset();
    if (!index.observe("asset://same", runtime_type_of<int>(), source_a).expired())
      return 13;
    index.release_unused();
    if (index.size() != 0U)
      return 7;
  }

  gneiss::engine::resource_cache cache;
  std::size_t load_count = 0;
  auto loader = [&load_count](std::shared_ptr<const void>& resource) {
    ++load_count;
    resource = std::make_shared<int>(42);
    return gneiss::engine::error::success;
  };
  gneiss::engine::asset_ref<const gneiss::engine::resource_cache::entry> first;
  gneiss::engine::asset_ref<const gneiss::engine::resource_cache::entry> second;
  if (load_cached(cache, "asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(),
                  loader, first) != gneiss::engine::error::success ||
      load_cached(cache, "asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(),
                  loader, second) != gneiss::engine::error::success ||
      load_count != 1U || first != second || cache.size() != 1U) {
    return 3;
  }
  auto reloader = [&load_count](std::shared_ptr<const void>& resource) {
    ++load_count;
    resource = std::make_shared<int>(84);
    return gneiss::engine::error::success;
  };
  if (load_cached(cache, "asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(),
                  reloader, second, {}, true) != gneiss::engine::error::success ||
      load_count != 2U || first == second ||
      *static_cast<const int*>(first->resource.get()) != 42 ||
      *static_cast<const int*>(second->resource.get()) != 84 || cache.size() != 1U) {
    return 3;
  }
  gneiss::engine::asset_ref<const gneiss::engine::resource_cache::entry> failed_reload;
  const auto failed_reloader = [](std::shared_ptr<const void>&) {
    return gneiss::engine::error::io;
  };
  if (load_cached(cache, "asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(),
                  failed_reloader, failed_reload, {}, true) != gneiss::engine::error::io ||
      failed_reload ||
      load_cached(cache, "asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(),
                  loader, failed_reload) != gneiss::engine::error::success ||
      failed_reload != second || load_count != 2U) {
    return 3;
  }
  // 抛异常与空结果都不能污染索引，重载失败仍可获取旧值。
  const auto throwing = [](std::shared_ptr<const void>&) -> gneiss::engine::error {
    throw std::bad_alloc{};
  };
  if (load_cached(cache, "asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(),
                  throwing, failed_reload, {}, true) != gneiss::engine::error::out_of_memory ||
      load_cached(cache, "asset://models/triangle.mesh", gneiss::engine::runtime_type_of<int>(),
                  loader, failed_reload) != gneiss::engine::error::success ||
      failed_reload != second)
    return 8;
  if (load_cached(cache, "asset://throws", gneiss::engine::runtime_type_of<int>(), throwing,
                  first) != gneiss::engine::error::out_of_memory ||
      cache.size() != 1U)
    return 8;
  const auto empty = [](std::shared_ptr<const void>&) { return gneiss::engine::error::success; };
  if (load_cached(cache, "asset://empty", gneiss::engine::runtime_type_of<int>(), empty, first) !=
          gneiss::engine::error::internal ||
      cache.size() != 1U)
    return 8;
  const auto recursive = [&](std::shared_ptr<const void>& output) {
    // 回调提前写入对象仍未完成创建，不能绕过同键递归保护。
    output = std::make_shared<int>(7);
    decltype(first) nested;
    if (load_cached(cache, "asset://recursive", gneiss::engine::runtime_type_of<int>(), loader,
                    nested) != gneiss::engine::error::invalid_state)
      return gneiss::engine::error::internal;
    cache.release_unused(); // 加载中的条目必须由当前调用保活。
    return gneiss::engine::error::success;
  };
  if (load_cached(cache, "asset://recursive", gneiss::engine::runtime_type_of<int>(), recursive,
                  first) != gneiss::engine::error::success ||
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
    return gneiss::engine::error::io;
  };
  if (load_cached(cache, "asset://models/broken.mesh", gneiss::engine::runtime_type_of<int>(),
                  failing_loader, first) != gneiss::engine::error::io ||
      load_cached(cache, "asset://models/broken.mesh", gneiss::engine::runtime_type_of<int>(),
                  failing_loader, first) != gneiss::engine::error::io ||
      failure_count != 2U || cache.size() != 0U) {
    return 5;
  }

  gneiss::engine::resource_cache other_cache;
  if (load_cached(other_cache, "asset://models/triangle.mesh",
                  gneiss::engine::runtime_type_of<int>(), loader,
                  first) != gneiss::engine::error::success ||
      load_count != 3U) {
    return 6;
  }
  return 0;
} catch (...) {
  return 99;
}
