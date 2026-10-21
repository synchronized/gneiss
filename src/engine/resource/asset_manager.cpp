// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "asset_manager.hpp"
#include "engine/platform/caors/adapter.hpp"
#include "engine/resource/backend/caors/caors_action_map.hpp"
#include "engine/resource/backend/caors/caors_vfs_bridge.hpp"
#include "engine/resource/vfs/asset_uri.hpp"
#include <map>
#include <stdexcept>

namespace gneiss::engine::resource {
// CAORS 管理、来源身份及调度生命周期归资产层；输入模块提供类型 Loader，Application 独立负责采用。
struct asset_manager::resource_state {
  std::shared_ptr<engine::platform::caors::worker_adapter> context;
  ::caors::tasks::external_executor worker;
  ::caors::tasks::execution_context completions;
  std::shared_ptr<engine::resource::caors_vfs_bridge> bridge;
  std::unique_ptr<engine::resource::action_map_loader> input_loader;
  ::caors::resources::cache<engine::resource::action_map_asset> input_cache;
  ::caors::assets::asset_manager manager;
  ::caors::resources::file_source source;
  std::map<std::string, ::caors::resource_guid, std::less<>> identities;
  std::uint64_t next_id = 1;
  resource_state(engine::resource::virtual_file_system files,
                 engine::core::tasks::task_executor& executor)
      : context(engine::platform::caors::make_worker(executor)), worker(context),
        bridge(engine::resource::make_caors_vfs_bridge(
            files, worker.get_executor(), engine::resource::maximum_action_map_bytes, true)),
        input_loader(engine::resource::make_action_map_loader(
            *bridge,
            [files](auto uri, auto original) {
              return engine::resource::verify_action_map_source(files, uri, original);
            })),
        manager({worker.get_executor(), completions.get_executor()}), source(bridge) {
    if (manager.register_type(input_cache, *input_loader,
                              ::caors::string_name{"gneiss.input.action_map"}))
      throw std::runtime_error("注册输入资源类型失败");
  }
  ~resource_state() {
    worker.close();
    input_loader->abandon();
    (void)completions.drain();
  }
};
asset_manager::asset_manager(engine::resource::virtual_file_system files)
    : files_(std::move(files)), prefabs_(files_, prefab_cache_) {}
asset_manager::~asset_manager() { stop_resources(); }
void asset_manager::attach_task_executor(engine::core::tasks::task_executor& executor) {
  if (!resource_state_) {
    resource_state_ = std::make_unique<resource_state>(files_, executor);
  }
}
result asset_manager::load_input(std::string_view uri,
                                 engine::resource::action_map& output) noexcept {
  return engine::resource::load_action_map(files_, uri, output);
}
result asset_manager::request_input(std::string_view uri,
                                    asset_request<action_map_asset>& output) noexcept {
  if (!resource_state_)
    return result::not_ready;
  if (engine::resource::validate_uri(uri) != GNEISS_SUCCESS)
    return result::invalid_argument;
  try {
    auto& s = *resource_state_;
    auto found = s.identities.find(uri);
    if (found == s.identities.end()) {
      ::caors::guid::byte_array bytes{};
      const auto number = s.next_id++;
      for (unsigned i = 0; i < 8; ++i)
        bytes[i] = static_cast<unsigned char>(number >> (i * 8));
      const ::caors::resource_guid id{::caors::guid::from_bytes(bytes)};
      if (s.manager.provider().bind(id, s.source, uri))
        return result::invalid_argument;
      found = s.identities.emplace(std::string(uri), id).first;
    }
    // 显式打开每次重新读取/复验，不能把上次缓存命中误当当前来源仍有效。
    auto request = s.input_cache.contains(found->second)
                       ? s.manager.request_reload<engine::resource::action_map_asset>(found->second)
                       : s.manager.request_load<engine::resource::action_map_asset>(found->second);
    output = asset_request<action_map_asset>{std::move(request)};
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}
void asset_manager::advance_resources() {
  if (!resource_state_)
    return;
  auto& s = *resource_state_;
  s.input_loader->pump();
  (void)s.completions.drain();
  s.context->collect();
}
void asset_manager::stop_resources() noexcept { resource_state_.reset(); }
}
