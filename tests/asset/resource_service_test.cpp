// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/native_file_system.h"
#include "asset/resource_cache.h"
#include "asset/virtual_file_system.h"

#include <gneiss/core/result.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

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
  temporary_directory directory;
  {
    std::ofstream stream(directory.path / "models" / "triangle.mesh", std::ios::binary);
    stream << "mesh";
  }

  auto provider = std::make_shared<gneiss::asset_internal::native_file_system>();
  gneiss::asset_internal::virtual_file_system file_system;
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

  const auto override_root = directory.path / "override";
  std::filesystem::create_directories(override_root);
  {
    std::ofstream stream(override_root / "special.mesh", std::ios::binary);
    stream << "override";
  }
  auto override_provider = std::make_shared<gneiss::asset_internal::native_file_system>();
  if (override_provider->initialize(override_root.string()) != GNEISS_SUCCESS ||
      file_system.mount("asset://models/", override_provider) != GNEISS_SUCCESS ||
      file_system.mount("asset://invalid", override_provider) != GNEISS_ERROR_INVALID_ARGUMENT ||
      file_system.read("asset://models/special.mesh", bytes) != GNEISS_SUCCESS ||
      bytes.size() != 8U || file_system.mount_count() != 2U) {
    return 2;
  }

  gneiss::asset_internal::resource_cache cache;
  std::size_t load_count = 0;
  auto loader = [&load_count](std::shared_ptr<void>& resource) {
    ++load_count;
    resource = std::make_shared<int>(42);
    return GNEISS_SUCCESS;
  };
  std::shared_ptr<const gneiss::asset_internal::resource_cache::entry> first;
  std::shared_ptr<const gneiss::asset_internal::resource_cache::entry> second;
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
  std::shared_ptr<const gneiss::asset_internal::resource_cache::entry> failed_reload;
  const auto failed_reloader = [](std::shared_ptr<void>&) { return GNEISS_ERROR_IO; };
  if (cache.reload("asset://models/triangle.mesh", 1U, failed_reloader, failed_reload) !=
          GNEISS_ERROR_IO ||
      failed_reload ||
      cache.acquire("asset://models/triangle.mesh", 1U, loader, failed_reload) != GNEISS_SUCCESS ||
      failed_reload != second || load_count != 2U) {
    return 3;
  }
  std::vector<gneiss::asset_internal::resource_cache::reload_request> transaction{
      {.uri = "asset://models/triangle.mesh", .type = 1U, .load = loader},
      {.uri = "asset://models/second.mesh", .type = 1U, .load = reloader}};
  std::vector<std::shared_ptr<const gneiss::asset_internal::resource_cache::entry>> committed;
  if (cache.reload_transaction(transaction, committed) != GNEISS_SUCCESS ||
      committed.size() != 2U || cache.size() != 2U) {
    return 4;
  }
  auto transaction_first = committed.front();
  transaction[1].load = failed_reloader;
  committed.clear();
  if (cache.reload_transaction(transaction, committed) != GNEISS_ERROR_IO || !committed.empty()) {
    return 4;
  }
  std::shared_ptr<const gneiss::asset_internal::resource_cache::entry> after_failure;
  if (cache.acquire("asset://models/triangle.mesh", 1U, loader, after_failure) != GNEISS_SUCCESS ||
      after_failure != transaction_first || load_count != 5U) {
    return 4;
  }
  first.reset();
  second.reset();
  failed_reload.reset();
  transaction_first.reset();
  after_failure.reset();
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

  gneiss::asset_internal::resource_cache other_cache;
  if (other_cache.acquire("asset://models/triangle.mesh", 1U, loader, first) != GNEISS_SUCCESS ||
      load_count != 6U) {
    return 6;
  }
  return 0;
} catch (...) {
  return 99;
}
