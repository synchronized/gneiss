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

bool deferred_adoption_contract() {
  using namespace gneiss::engine::resource;
  resource_cache cache;
  const asset_identity identity{.source = std::make_shared<asset_source_domain>(),
                                .uri = "asset://deferred.mesh",
                                .type = 1U};
  const resource_cache::selection_key selection{.identity = identity, .configuration = "profile"};
  const resource_cache::version_key first{.identity = identity, .version = "a"};
  const resource_cache::version_key second{.identity = identity, .version = "b"};
  const auto load = [](std::shared_ptr<void>& value) {
    value = std::make_shared<int>(42);
    return GNEISS_SUCCESS;
  };
  std::shared_ptr<const resource_cache::entry> a;
  std::shared_ptr<const resource_cache::entry> b;
  const std::array adopt_a{
      resource_cache::version_selection{.selection = selection, .version = first}};
  const std::array adopt_b{
      resource_cache::version_selection{.selection = selection, .version = second}};
  if (cache.acquire(first, load, a) != GNEISS_SUCCESS ||
      cache.acquire(second, load, b) != GNEISS_SUCCESS ||
      cache.adopt_versions(adopt_a) != GNEISS_SUCCESS) {
    return false;
  }
  std::unique_ptr<resource_cache::prepared_adoption> pending;
  std::unique_ptr<resource_cache::prepared_adoption> competing;
  const auto revision = cache.revision();
  if (cache.prepare_adoption(adopt_b, pending) != GNEISS_SUCCESS ||
      cache.observe_selected(selection).lock() != a || cache.revision() != revision ||
      cache.prepare_adoption(adopt_a, competing) != GNEISS_ERROR_NOT_READY || competing ||
      cache.adopt_versions(adopt_a) != GNEISS_ERROR_NOT_READY ||
      cache.prepare_adoption(adopt_a, pending) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return false;
  }
  pending.reset();
  if (cache.observe_selected(selection).lock() != a || cache.revision() != revision ||
      cache.prepare_adoption(adopt_b, pending) != GNEISS_SUCCESS) {
    return false;
  }
  const std::weak_ptr<const resource_cache::entry> retained = b;
  b.reset();
  cache.release_unused();
  if (retained.expired() || cache.observe_selected(selection).lock() != a) {
    return false;
  }
  pending->commit();
  if (cache.observe_selected(selection).lock() != retained.lock()) {
    return false;
  }
  const auto committed = cache.revision();
  pending->commit();
  if (cache.revision() != committed || cache.adopt_versions(adopt_a) != GNEISS_SUCCESS) {
    return false;
  }
  pending.reset();
  cache.release_unused();
  if (!retained.expired() || cache.observe_selected(selection).lock() != a) {
    return false;
  }
  // 回调递归采用不能抢走外层事务的采用权。
  return cache.adopt_versions(adopt_a, [&] {
    return cache.adopt_versions(adopt_a) == GNEISS_ERROR_NOT_READY ? GNEISS_SUCCESS
                                                                   : GNEISS_ERROR_INTERNAL;
  }) == GNEISS_SUCCESS;
}

bool selection_contract() {
  if (!deferred_adoption_contract()) {
    return false;
  }
  using namespace gneiss::engine::resource;
  resource_cache cache;
  const asset_identity identity{
      .source = std::make_shared<asset_source_domain>(),
      .uri = "asset://selected.mesh",
      .type = 1U,
  };
  const resource_cache::selection_key selection{.identity = identity, .configuration = "profile-a"};
  const resource_cache::version_key first{.identity = identity, .version = "a"};
  const resource_cache::version_key second{.identity = identity, .version = "b"};
  const auto load = [](std::shared_ptr<void>& value) {
    value = std::make_shared<int>(42);
    return GNEISS_SUCCESS;
  };
  std::shared_ptr<const resource_cache::entry> a;
  std::shared_ptr<const resource_cache::entry> b;
  if (cache.acquire(first, load, a) != GNEISS_SUCCESS ||
      !cache.observe_selected(selection).expired()) {
    return false;
  }
  const std::array adopt_a{
      resource_cache::version_selection{.selection = selection, .version = first},
  };
  if (cache.adopt_versions(adopt_a) != GNEISS_SUCCESS ||
      cache.observe_selected(selection).lock() != a ||
      cache.acquire(second, load, b) != GNEISS_SUCCESS ||
      cache.observe_selected(selection).lock() != a) {
    return false;
  }
  const auto revision = cache.revision();
  if (cache.adopt_versions(adopt_a) != GNEISS_SUCCESS || cache.revision() != revision) {
    return false;
  }
  auto another_profile = selection;
  another_profile.configuration = "profile-b";
  const std::array adopt_b{
      resource_cache::version_selection{.selection = selection, .version = second},
  };
  unsigned accepted = 0U;
  const auto reject = [&] {
    ++accepted;
    return GNEISS_ERROR_IO;
  };
  if (cache.adopt_versions(adopt_b, reject) != GNEISS_ERROR_IO || accepted != 1U ||
      cache.revision() != revision || cache.observe_selected(selection).lock() != a ||
      cache.adopt_versions(adopt_b, {}) != GNEISS_ERROR_INVALID_ARGUMENT ||
      cache.adopt_versions(adopt_b, []() -> gneiss_result { throw 42; }) != GNEISS_ERROR_INTERNAL ||
      cache.revision() != revision || cache.observe_selected(selection).lock() != a) {
    return false;
  }
  auto missing = first;
  missing.version = "missing";
  const std::array invalid_batch{
      adopt_b.front(),
      resource_cache::version_selection{.selection = another_profile, .version = missing},
  };
  if (cache.adopt_versions(invalid_batch) != GNEISS_ERROR_NOT_READY ||
      cache.revision() != revision || cache.observe_selected(selection).lock() != a ||
      !cache.observe_selected(another_profile).expired()) {
    return false;
  }
  if (cache.adopt_versions(invalid_batch, reject) != GNEISS_ERROR_NOT_READY || accepted != 1U) {
    return false;
  }
  const std::array duplicate{adopt_b.front(), adopt_b.front()};
  auto foreign = selection;
  foreign.identity.source = std::make_shared<asset_source_domain>();
  const std::array wrong_source{
      resource_cache::version_selection{.selection = foreign, .version = first},
  };
  if (cache.adopt_versions(duplicate) != GNEISS_ERROR_INVALID_ARGUMENT ||
      cache.adopt_versions(wrong_source) != GNEISS_ERROR_INVALID_ARGUMENT ||
      cache.adopt_versions({}) != GNEISS_ERROR_INVALID_ARGUMENT ||
      cache.observe_selected(selection).lock() != a || !cache.observe_selected(foreign).expired()) {
    return false;
  }
  const std::array both{
      adopt_b.front(),
      resource_cache::version_selection{
          .selection = another_profile,
          .version = first,
      },
  };
  const auto accept = [&] {
    ++accepted;
    // 使用者提交时默认选择仍是旧版本，候选映射尚未发布。
    return cache.observe_selected(selection).lock() == a ? GNEISS_SUCCESS
                                                         : GNEISS_ERROR_INVALID_STATE;
  };
  if (cache.adopt_versions(both, accept) != GNEISS_SUCCESS || accepted != 2U ||
      cache.observe_selected(selection).lock() != b ||
      cache.observe_selected(another_profile).lock() != a || cache.observe(first).lock() != a) {
    return false;
  }
  const auto adopted_revision = cache.revision();
  // 映射相同也必须提交使用者，例如新实例获取已采用版本。
  if (cache.adopt_versions(both,
                           [&] {
                             ++accepted;
                             return GNEISS_SUCCESS;
                           }) != GNEISS_SUCCESS ||
      accepted != 3U || cache.revision() != adopted_revision) {
    return false;
  }
  b.reset();
  cache.release_unused();
  if (!cache.observe_selected(selection).expired() ||
      cache.observe_selected(another_profile).lock() != a) {
    return false;
  }
  a.reset();
  cache.release_unused();
  return cache.size() == 0U && cache.observe_selected(another_profile).expired();
}

bool version_cache_contract() {
  using namespace gneiss::engine::resource;
  resource_cache cache;
  resource_cache::version_key key{.identity = {.source = std::make_shared<asset_source_domain>(),
                                               .uri = "asset://shared.mesh",
                                               .type = 1U},
                                  .version = "v1"};
  std::shared_ptr<const resource_cache::entry> first;
  std::shared_ptr<const resource_cache::entry> second;
  int loads = 0;
  const auto load = [&](std::shared_ptr<void>& output) {
    output = std::make_shared<int>(++loads);
    return GNEISS_SUCCESS;
  };
  if (cache.acquire(key, load, first) != GNEISS_SUCCESS ||
      cache.acquire(key, load, second) != GNEISS_SUCCESS || first != second || loads != 1) {
    return false;
  }
  auto next = key;
  next.version = "v2";
  const auto fail = [](std::shared_ptr<void>&) { return GNEISS_ERROR_IO; };
  if (cache.acquire(next, fail, second) != GNEISS_ERROR_IO || second ||
      cache.observe(next).lock() || cache.observe(key).lock() != first || cache.size() != 1U) {
    return false;
  }
  const auto throwing = [](std::shared_ptr<void>&) -> gneiss_result { throw 1; };
  if (cache.acquire(next, throwing, second) != GNEISS_ERROR_INTERNAL || cache.size() != 1U ||
      cache.acquire(next, load, second) != GNEISS_SUCCESS || second == first || loads != 2 ||
      *static_cast<const int*>(first->resource.get()) != 1) {
    return false;
  }
  const auto old = cache.observe(key);
  first.reset();
  cache.release_unused();
  if (!old.expired() || cache.observe(next).lock() != second || cache.size() != 1U) {
    return false;
  }
  auto foreign = next;
  foreign.identity.source = std::make_shared<asset_source_domain>();
  if (cache.acquire(foreign, load, first) != GNEISS_SUCCESS || first == second || loads != 3) {
    return false;
  }
  auto typed = next;
  typed.identity.type = 2U;
  if (cache.acquire(typed, load, first) != GNEISS_SUCCESS || first == second || loads != 4) {
    return false;
  }
  // 旧 URI 热重载不得覆盖固定版本条目。
  if (cache.reload(next.identity.uri, 1U, load, first) != GNEISS_SUCCESS ||
      cache.observe(next).lock() != second ||
      *static_cast<const int*>(second->resource.get()) != 2) {
    return false;
  }
  auto invalid = key;
  invalid.version.clear();
  if (cache.acquire(invalid, load, first) != GNEISS_ERROR_INVALID_ARGUMENT || first) {
    return false;
  }
  invalid = key;
  invalid.identity.source.reset();
  if (cache.acquire(invalid, load, first) != GNEISS_ERROR_INVALID_ARGUMENT || first) {
    return false;
  }
  auto recursive_key = key;
  recursive_key.version = "recursive";
  const auto recursive = [&](std::shared_ptr<void>& output) {
    std::shared_ptr<const resource_cache::entry> nested;
    if (cache.acquire(recursive_key, load, nested) != GNEISS_ERROR_INVALID_STATE || nested) {
      return GNEISS_ERROR_INTERNAL;
    }
    return load(output);
  };
  if (cache.acquire(recursive_key, recursive, first) != GNEISS_SUCCESS) {
    return false;
  }
  first.reset();
  second.reset();
  cache.release_unused();
  return cache.size() == 0U;
}

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
  if (!source_identity_contract() || !version_cache_contract() || !selection_contract()) {
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
