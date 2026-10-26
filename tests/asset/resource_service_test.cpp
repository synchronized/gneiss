// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include "engine/resource/cache/resource_cache.hpp"
#include "engine/resource/vfs/native_file_system.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

bool source_identity_contract() {
  using namespace gneiss::engine;
  auto backend = std::make_shared<native_file_system>();
  virtual_file_system first;
  asset_identity original;
  if (first.identify("asset://model.mesh", 1U, original) != gneiss::engine::result::not_found ||
      first.mount("asset://", backend) != gneiss::engine::result::success ||
      first.identify("asset://model.mesh", 1U, original) != gneiss::engine::result::success) {
    return false;
  }
  // 未初始化文件后端仍能解析身份：本接口不应引入同步 I/O。
  auto copied = first;
  asset_identity observed;
  if (copied.identify(original.uri, original.type, observed) != gneiss::engine::result::success ||
      observed != original) {
    return false;
  }
  virtual_file_system independent;
  if (independent.mount("asset://", backend) != gneiss::engine::result::success ||
      independent.identify(original.uri, original.type, observed) !=
          gneiss::engine::result::success ||
      observed == original || observed.source == original.source) {
    return false;
  }
  if (first.identify(original.uri, 2U, observed) != gneiss::engine::result::success ||
      observed == original ||
      first.identify("asset://Model.mesh", 1U, observed) != gneiss::engine::result::success ||
      observed == original) {
    return false;
  }
  asset_identity before_overlay;
  if (copied.identify("asset://nested/model.mesh", 1U, before_overlay) !=
          gneiss::engine::result::success ||
      copied.mount("asset://nested/", backend) != gneiss::engine::result::success ||
      copied.identify(before_overlay.uri, 1U, observed) != gneiss::engine::result::success ||
      observed.source == before_overlay.source ||
      first.identify(before_overlay.uri, 1U, observed) != gneiss::engine::result::success ||
      observed != before_overlay) {
    return false;
  }
  if (first.identify("asset://../bad", 1U, observed) != gneiss::engine::result::invalid_argument ||
      observed.source || !observed.uri.empty() || observed.type != 0U ||
      first.identify(original.uri, 0U, observed) != gneiss::engine::result::invalid_argument) {
    return false;
  }
  const std::weak_ptr<const file_source_domain> source = original.source;
  first = {};
  copied = {};
  before_overlay = {};
  if (source.expired()) {
    return false;
  }
  original = {};
  return source.expired();
}

struct temporary_directory final {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("gneiss-asset-test-" + std::to_string(std::hash<std::string>{}(__FILE__)));
  temporary_directory() {
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path / "models");
  }
  ~temporary_directory() noexcept {
    try {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
      // NOLINTNEXTLINE(bugprone-empty-catch): 析构期间只能尽力清理测试临时目录。
    } catch (...) {
    }
  }
};

} // namespace

int main() try {
  if (!source_identity_contract()) {
    return 30;
  }
  temporary_directory directory;
  {
    std::ofstream stream(directory.path / "models" / "triangle.mesh", std::ios::binary);
    stream << "mesh";
  }

  auto provider = std::make_shared<gneiss::engine::native_file_system>();
  gneiss::engine::virtual_file_system file_system;
  std::vector<std::byte> bytes;
  if (provider->read("models/triangle.mesh", bytes) != gneiss::engine::result::invalid_state ||
      provider->initialize(directory.path.string()) != gneiss::engine::result::success ||
      !provider->is_initialized() ||
      file_system.mount("asset://", provider) != gneiss::engine::result::success ||
      file_system.mount("asset://", provider) != gneiss::engine::result::invalid_state ||
      file_system.read("asset://models/triangle.mesh", bytes) != gneiss::engine::result::success ||
      bytes.size() != 4U ||
      file_system.read("asset://models/missing.mesh", bytes) != gneiss::engine::result::not_found ||
      file_system.read("asset://../outside", bytes) != gneiss::engine::result::invalid_argument) {
    return 1;
  }

  const auto previous = bytes;
  if (file_system.read_bounded("asset://models/triangle.mesh", 3U, bytes) !=
          gneiss::engine::result::invalid_argument ||
      bytes != previous ||
      file_system.read_bounded("asset://models/triangle.mesh", 4U, bytes) !=
          gneiss::engine::result::success ||
      bytes.size() != 4U ||
      file_system.read_bounded("asset://../outside", 4U, bytes) !=
          gneiss::engine::result::invalid_argument) {
    return 20;
  }

  const auto override_root = directory.path / "override";
  std::filesystem::create_directories(override_root);
  {
    std::ofstream stream(override_root / "special.mesh", std::ios::binary);
    stream << "override";
  }
  auto override_provider = std::make_shared<gneiss::engine::native_file_system>();
  if (override_provider->initialize(override_root.string()) != gneiss::engine::result::success ||
      file_system.mount("asset://models/", override_provider) != gneiss::engine::result::success ||
      file_system.mount("asset://invalid", override_provider) !=
          gneiss::engine::result::invalid_argument ||
      file_system.read("asset://models/special.mesh", bytes) != gneiss::engine::result::success ||
      bytes.size() != 8U || file_system.mount_count() != 2U) {
    return 2;
  }

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
