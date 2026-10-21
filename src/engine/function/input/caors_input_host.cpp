// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "caors_input_host.hpp"
#include "caors_action_map.hpp"
#include "engine/asset/asset_uri.hpp"
#include "engine/asset/caors_vfs_bridge.hpp"
#include "engine/core/sha256.hpp"
#include "engine/platform/caors/adapter.hpp"
#include <map>
#include <stdexcept>
namespace gneiss::input_internal {
struct caors_input_host::storage {
  std::shared_ptr<caors_internal::worker_adapter> context;
  caors::tasks::external_executor worker;
  caors::tasks::execution_context completions;
  std::shared_ptr<asset_internal::caors_vfs_bridge> bridge;
  std::unique_ptr<action_map_loader> loader;
  caors::resources::cache<action_map_asset> cache;
  caors::assets::asset_manager manager;
  caors::resources::file_source source;
  action_map_application application;
  std::map<std::string, caors::resource_guid, std::less<>> identities;
  std::uint64_t next_id = 1;
  result latest = result::invalid_state;
  storage(asset_internal::virtual_file_system files, tasks::task_executor& executor)
      : context(caors_internal::make_worker(executor)), worker(context),
        bridge(asset_internal::make_caors_vfs_bridge(files, worker.get_executor(), 1024U * 1024U, true)),
        loader(make_action_map_loader(
            *bridge,
            [files](auto uri, auto original) {
              // 限额与 Loader 一致；在 worker 使用既有 VFS/libuv 路径复读，不建立新挂载。
              std::vector<std::byte> current;
              const result read{files.read_bounded(uri, 1024U * 1024U, current)};
              if (read != result::success)
                return read;
              return current.size() == original.size() &&
                             core::sha256(current) == core::sha256(original)
                         ? result::success
                         : result::invalid_state;
            })),
        manager({worker.get_executor(), completions.get_executor()}), source(bridge) {
    if (manager.register_type(cache, *loader, caors::string_name{"gneiss.input.action_map"}))
      throw std::runtime_error("注册输入资源类型失败");
  }
  ~storage() {
    application.cancel();
    worker.close();
    loader->abandon();
    (void)completions.drain();
  }
};
caors_input_host::caors_input_host(asset_internal::virtual_file_system files,
                                   tasks::task_executor& executor)
    : storage_(std::make_unique<storage>(std::move(files), executor)) {}
caors_input_host::~caors_input_host() = default;
result caors_input_host::begin(std::string_view uri) noexcept {
  if (asset_internal::validate_uri(uri) != GNEISS_SUCCESS)
    return result::invalid_argument;
  try {
    auto& s = *storage_;
    auto found = s.identities.find(uri);
    if (found == s.identities.end()) {
      caors::guid::byte_array bytes{};
      const auto number = s.next_id++;
      for (unsigned i = 0; i < 8; ++i)
        bytes[i] = static_cast<unsigned char>(number >> (i * 8));
      const caors::resource_guid id{caors::guid::from_bytes(bytes)};
      if (s.manager.provider().bind(id, s.source, uri))
        return result::invalid_argument;
      found = s.identities.emplace(std::string(uri), id).first;
    }
    // 显式打开每次重新读取/复验，不能把上次缓存命中误当当前来源仍有效。
    const auto started =
        s.application.begin(s.manager, found->second, s.cache.contains(found->second));
    s.latest = started == result::success ? result::not_ready : started;
    return started;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}
void caors_input_host::cancel() noexcept {
  storage_->application.cancel();
  storage_->latest = result::invalid_state;
}
result caors_input_host::status() const noexcept { return storage_->latest; }
void caors_input_host::advance(const std::function<result(action_map)>& apply) {
  auto& s = *storage_;
  s.loader->pump();
  (void)s.completions.drain();
  s.context->collect();
  if (s.latest == result::not_ready)
    s.latest = s.application.poll(apply);
}
}
