// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "asset_manager.hpp"
#include "engine/asset/asset_uri.hpp"
#include "engine/asset/caors_vfs_bridge.hpp"
#include "engine/function/input/caors_action_map.hpp"
#include "engine/platform/caors/adapter.hpp"
#include <map>
#include <stdexcept>

namespace gneiss::engine {
// CAORS 管理、来源身份及调度生命周期归资产层；输入模块只提供类型 Loader 和采用状态。
struct asset_manager::resource_state {
  std::shared_ptr<caors_internal::worker_adapter> context;
  caors::tasks::external_executor worker;
  caors::tasks::execution_context completions;
  std::shared_ptr<asset_internal::caors_vfs_bridge> bridge;
  std::unique_ptr<input_internal::action_map_loader> input_loader;
  caors::resources::cache<input_internal::action_map_asset> input_cache;
  caors::assets::asset_manager manager;
  caors::resources::file_source source;
  input_internal::action_map_application input_application;
  std::map<std::string, caors::resource_guid, std::less<>> identities;
  std::uint64_t next_id = 1;
  result input_status = result::invalid_state;
  resource_state(asset_internal::virtual_file_system files, tasks::task_executor& executor)
      : context(caors_internal::make_worker(executor)), worker(context),
        bridge(asset_internal::make_caors_vfs_bridge(
            files, worker.get_executor(), input_internal::maximum_action_map_bytes, true)),
        input_loader(input_internal::make_action_map_loader(
            *bridge,
            [files](auto uri, auto original) {
              return input_internal::verify_action_map_source(files, uri, original);
            })),
        manager({worker.get_executor(), completions.get_executor()}), source(bridge) {
    if (manager.register_type(input_cache, *input_loader,
                              caors::string_name{"gneiss.input.action_map"}))
      throw std::runtime_error("注册输入资源类型失败");
  }
  ~resource_state() {
    input_application.cancel();
    worker.close();
    input_loader->abandon();
    (void)completions.drain();
  }
};
asset_manager::asset_manager(asset_internal::virtual_file_system files,
                             render_internal::render_resource_service& resources)
    : files_(std::move(files)), render_(files_, cache_, resources), prefabs_(files_, cache_) {}
asset_manager::~asset_manager() { stop_input(); }
void asset_manager::attach_input_loader(tasks::task_executor& executor) {
  if (!resource_state_) {
    resource_state_ = std::make_unique<resource_state>(files_, executor);
  }
}
result asset_manager::load_input(std::string_view uri,
                                 input_internal::action_map& output) noexcept {
  (void)cancel_input();
  return input_internal::load_action_map(files_, uri, output);
}
result asset_manager::begin_input(std::string_view uri) noexcept {
  if (!resource_state_)
    return result::not_ready;
  if (asset_internal::validate_uri(uri) != GNEISS_SUCCESS)
    return result::invalid_argument;
  try {
    auto& s = *resource_state_;
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
    s.input_application.cancel();
    auto request = s.input_cache.contains(found->second)
                       ? s.manager.request_reload<input_internal::action_map_asset>(found->second)
                       : s.manager.request_load<input_internal::action_map_asset>(found->second);
    const auto started = s.input_application.begin(std::move(request));
    s.input_status = started == result::success ? result::not_ready : started;
    return started;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}
result asset_manager::input_status() const noexcept {
  return resource_state_ ? resource_state_->input_status : result::not_ready;
}
result asset_manager::cancel_input() noexcept {
  if (!resource_state_)
    return result::not_ready;
  resource_state_->input_application.cancel();
  resource_state_->input_status = result::invalid_state;
  return result::success;
}
void asset_manager::advance_input(const std::function<result(input_internal::action_map)>& apply) {
  if (!resource_state_)
    return;
  auto& s = *resource_state_;
  s.input_loader->pump();
  (void)s.completions.drain();
  s.context->collect();
  if (s.input_status == result::not_ready)
    s.input_status = s.input_application.poll(apply);
}
void asset_manager::stop_input() noexcept { resource_state_.reset(); }
}
