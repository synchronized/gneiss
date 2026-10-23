// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/cache/resource_cache.hpp"
#include "engine/resource/vfs/native_file_system.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"

#include <gneiss/engine/core/result.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

bool source_identity_contract() {
  using namespace gneiss::engine::resource;
  auto backend = std::make_shared<native_file_system>();
  virtual_file_system first;
  asset_identity original;
  if (first.identify("asset://model.mesh", 1U, original) != GNEISS_ERROR_NOT_FOUND ||
      first.mount("asset://", backend) != GNEISS_SUCCESS ||
      first.identify("asset://model.mesh", 1U, original) != GNEISS_SUCCESS) {
    return false;
  }
  // 未初始化文件后端仍能解析身份：本接口不应引入同步 I/O。
  auto copied = first;
  asset_identity observed;
  if (copied.identify(original.uri, original.type, observed) != GNEISS_SUCCESS ||
      observed != original) {
    return false;
  }
  virtual_file_system independent;
  if (independent.mount("asset://", backend) != GNEISS_SUCCESS ||
      independent.identify(original.uri, original.type, observed) != GNEISS_SUCCESS ||
      observed == original || observed.source == original.source) {
    return false;
  }
  if (first.identify(original.uri, 2U, observed) != GNEISS_SUCCESS || observed == original ||
      first.identify("asset://Model.mesh", 1U, observed) != GNEISS_SUCCESS ||
      observed == original) {
    return false;
  }
  asset_identity before_overlay;
  if (copied.identify("asset://nested/model.mesh", 1U, before_overlay) != GNEISS_SUCCESS ||
      copied.mount("asset://nested/", backend) != GNEISS_SUCCESS ||
      copied.identify(before_overlay.uri, 1U, observed) != GNEISS_SUCCESS ||
      observed.source == before_overlay.source ||
      first.identify(before_overlay.uri, 1U, observed) != GNEISS_SUCCESS ||
      observed != before_overlay) {
    return false;
  }
  if (first.identify("asset://../bad", 1U, observed) != GNEISS_ERROR_INVALID_ARGUMENT ||
      observed.source || !observed.uri.empty() || observed.type != 0U ||
      first.identify(original.uri, 0U, observed) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return false;
  }
  const std::weak_ptr<const asset_source_domain> source = original.source;
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

  auto provider = std::make_shared<gneiss::engine::resource::native_file_system>();
  gneiss::engine::resource::virtual_file_system file_system;
  std::vector<std::byte> bytes;
  if (provider->read("models/triangle.mesh", bytes) != GNEISS_ERROR_INVALID_STATE ||
      provider->initialize(directory.path.string()) != GNEISS_SUCCESS ||
      !provider->is_initialized() || file_system.mount("asset://", provider) != GNEISS_SUCCESS ||
      file_system.mount("asset://", provider) != GNEISS_ERROR_INVALID_STATE ||
      file_system.read("asset://models/triangle.mesh", bytes) != GNEISS_SUCCESS ||
      bytes.size() != 4U ||
      file_system.read("asset://models/missing.mesh", bytes) != GNEISS_ERROR_NOT_FOUND ||
      file_system.read("asset://../outside", bytes) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 1;
  }

  const auto previous = bytes;
  if (file_system.read_bounded("asset://models/triangle.mesh", 3U, bytes) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      bytes != previous ||
      file_system.read_bounded("asset://models/triangle.mesh", 4U, bytes) != GNEISS_SUCCESS ||
      bytes.size() != 4U ||
      file_system.read_bounded("asset://../outside", 4U, bytes) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 20;
  }

  const auto override_root = directory.path / "override";
  std::filesystem::create_directories(override_root);
  {
    std::ofstream stream(override_root / "special.mesh", std::ios::binary);
    stream << "override";
  }
  auto override_provider = std::make_shared<gneiss::engine::resource::native_file_system>();
  if (override_provider->initialize(override_root.string()) != GNEISS_SUCCESS ||
      file_system.mount("asset://models/", override_provider) != GNEISS_SUCCESS ||
      file_system.mount("asset://invalid", override_provider) != GNEISS_ERROR_INVALID_ARGUMENT ||
      file_system.read("asset://models/special.mesh", bytes) != GNEISS_SUCCESS ||
      bytes.size() != 8U || file_system.mount_count() != 2U) {
    return 2;
  }

  gneiss::engine::resource::resource_cache cache;
  std::size_t load_count = 0;
  auto loader = [&load_count](std::shared_ptr<void>& resource) {
    ++load_count;
    resource = std::make_shared<int>(42);
    return GNEISS_SUCCESS;
  };
  std::shared_ptr<const gneiss::engine::resource::resource_cache::entry> first;
  std::shared_ptr<const gneiss::engine::resource::resource_cache::entry> second;
  if (cache.acquire("asset://models/triangle.mesh", 1U, loader, first) != GNEISS_SUCCESS ||
      cache.acquire("asset://models/triangle.mesh", 1U, loader, second) != GNEISS_SUCCESS ||
      load_count != 1U || first != second || cache.size() != 1U ||
      cache.acquire("asset://models/triangle.mesh", 2U, loader, second) !=
          GNEISS_ERROR_INVALID_ARGUMENT) {
    return 3;
  }
  auto reloader = [&load_count](std::shared_ptr<void>& resource) {
    ++load_count;
    resource = std::make_shared<int>(84);
    return GNEISS_SUCCESS;
  };
  if (cache.reload("asset://models/triangle.mesh", 1U, reloader, second) != GNEISS_SUCCESS ||
      load_count != 2U || first == second ||
      *static_cast<const int*>(first->resource.get()) != 42 ||
      *static_cast<const int*>(second->resource.get()) != 84 || cache.size() != 1U) {
    return 3;
  }
  std::shared_ptr<const gneiss::engine::resource::resource_cache::entry> failed_reload;
  const auto failed_reloader = [](std::shared_ptr<void>&) { return GNEISS_ERROR_IO; };
  if (cache.reload("asset://models/triangle.mesh", 1U, failed_reloader, failed_reload) !=
          GNEISS_ERROR_IO ||
      failed_reload ||
      cache.acquire("asset://models/triangle.mesh", 1U, loader, failed_reload) != GNEISS_SUCCESS ||
      failed_reload != second || load_count != 2U) {
    return 3;
  }
  first.reset();
  second.reset();
  failed_reload.reset();
  cache.release_unused();
  if (cache.size() != 0U) {
    return 4;
  }

  std::size_t failure_count = 0;
  auto failing_loader = [&failure_count](std::shared_ptr<void>&) {
    ++failure_count;
    return GNEISS_ERROR_IO;
  };
  if (cache.acquire("asset://models/broken.mesh", 1U, failing_loader, first) != GNEISS_ERROR_IO ||
      cache.acquire("asset://models/broken.mesh", 1U, failing_loader, first) != GNEISS_ERROR_IO ||
      failure_count != 2U || cache.size() != 0U) {
    return 5;
  }

  gneiss::engine::resource::resource_cache other_cache;
  if (other_cache.acquire("asset://models/triangle.mesh", 1U, loader, first) != GNEISS_SUCCESS ||
      load_count != 3U) {
    return 6;
  }
  return 0;
} catch (...) {
  return 99;
}
