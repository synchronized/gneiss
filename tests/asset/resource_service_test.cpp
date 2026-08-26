// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/directory_asset_provider.h"
#include "asset/resource_cache.h"

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

  gneiss::asset_internal::directory_asset_provider provider;
  std::vector<std::byte> bytes;
  if (provider.read("asset://models/triangle.mesh", bytes) != GNEISS_ERROR_INVALID_STATE ||
      provider.mount(directory.path.string()) != GNEISS_SUCCESS || !provider.is_mounted() ||
      provider.read("asset://models/triangle.mesh", bytes) != GNEISS_SUCCESS ||
      bytes.size() != 4U ||
      provider.read("asset://models/missing.mesh", bytes) != GNEISS_ERROR_NOT_FOUND ||
      provider.read("asset://../outside", bytes) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 1;
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
    return 2;
  }
  first.reset();
  second.reset();
  cache.release_unused();
  if (cache.size() != 0U) {
    return 3;
  }

  std::size_t failure_count = 0;
  auto failing_loader = [&failure_count](std::shared_ptr<void>&) {
    ++failure_count;
    return GNEISS_ERROR_IO;
  };
  if (cache.acquire("asset://models/broken.mesh", 1U, failing_loader, first) != GNEISS_ERROR_IO ||
      cache.acquire("asset://models/broken.mesh", 1U, failing_loader, first) != GNEISS_ERROR_IO ||
      failure_count != 2U || cache.size() != 0U) {
    return 4;
  }

  gneiss::asset_internal::resource_cache other_cache;
  if (other_cache.acquire("asset://models/triangle.mesh", 1U, loader, first) != GNEISS_SUCCESS ||
      load_count != 2U) {
    return 5;
  }
  return 0;
} catch (...) {
  return 99;
}
