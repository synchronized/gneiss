// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/loading/scene_service.hpp"
#include "engine/core/diagnostics/loop_timing.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/loading/resource_load_group.hpp"
#include "runtime/loading/scene_domain.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <map>
#include <utility>

namespace gneiss::runtime {

gneiss::engine::error scene_service::initialize(gneiss::engine::asset_manager& resources) {
  if (active_ || stopping_) {
    return gneiss::engine::error::invalid_state;
  }
  auto next = std::make_unique<scene_domain>();
  const auto result = next->initialize(resources);
  if (result != gneiss::engine::error::success) {
    return result;
  }
  resources_ = &resources;
  active_ = std::move(next);
  return gneiss::engine::error::success;
}

gneiss::engine::error
scene_service::configure(gneiss::engine::render_service& renderer,
                         std::shared_ptr<gneiss::engine::progress_notification> notification) {
  if (loading_ || !active_ || stopping_) {
    return gneiss::engine::error::invalid_state;
  }
  renderer_ = &renderer;
  notification_ = std::move(notification);
  loading_ = std::make_unique<resource_load_group>(*resources_);
  return gneiss::engine::error::success;
}

void scene_service::advance() {
  check_owner();
  if (stopping_) {
    advance_stop();
    return;
  }
  if (retired_) {
    const auto start = std::chrono::steady_clock::now();
    const gneiss::engine::loop_span span(gneiss::engine::loop_stage::retirement);
    retired_.reset();
    resources_->collect_unused();
    retirement_.last_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    ++retirement_.retired_domains;
  }
  advance_visuals();
  advance_load();
  advance_reload();
}

void scene_service::visual_load_state::advance(resource_load_group& group) {
  if (completion) {
    return;
  }
  if (pending) {
    group.advance();
    gneiss::runtime::asset_load_completion finished;
    if (!group.take(finished)) {
      return;
    }
    pending = false;
    completion = std::move(finished);
    return;
  }
  if (cursor == queue.size()) {
    queue.clear();
    cursor = 0U;
    return;
  }
  std::uint64_t request{};
  const auto submitted =
      group.submit_assets(std::span(queue).subspan(cursor, 1U), session, revision, request, false);
  if (submitted == gneiss::engine::error::not_ready) {
    return;
  }
  if (submitted == gneiss::engine::error::success) {
    pending = true;
  } else {
    gneiss::runtime::asset_load_completion finished;
    finished.result = submitted;
    finished.results.push_back({.source = queue[cursor], .result = submitted});
    completion = std::move(finished);
  }
  ++cursor;
}

bool scene_service::visual_load_state::take(gneiss::runtime::asset_load_completion& output) {
  if (!completion) {
    return false;
  }
  output = std::move(*completion);
  completion.reset();
  return true;
}

void scene_service::visual_load_state::begin(std::vector<engine::render_resource_source> sources,
                                             std::uint64_t next_session,
                                             std::uint64_t next_revision) {
  queue = std::move(sources);
  cursor = 0;
  session = next_session;
  revision = next_revision;
}
void scene_service::visual_load_state::cancel(resource_load_group& group) {
  queue.clear();
  cursor = 0;
  // 取消回执仍由 advance/take 消费，不能提前清除 pending。
  if (pending)
    (void)group.cancel();
}
void scene_service::visual_load_state::stop(resource_load_group* group) {
  queue.clear();
  completion.reset();
  if (group)
    group->request_stop();
}
void scene_service::visual_load_state::reset() noexcept { *this = {}; }
void scene_service::advance_visuals() {
  if (loading_)
    visuals_.advance(*loading_);
}
bool scene_service::take_visual_completion(asset_load_completion& output) {
  return visuals_.take(output);
}

void scene_service::shutdown() noexcept {
  stopping_ = true;
  visuals_.reset();
  reload_.reset();
  stop_load();
  loading_.reset();
  retired_.reset();
  active_.reset();
  if (resources_ != nullptr) {
    resources_->collect_unused();
  }
  resources_ = nullptr;
}

bool scene_service::can_start_load() const noexcept {
  return !stopping_ && !reload_ && !retired_ && loading_ && (!loading_->busy() || visuals_.pending);
}
bool scene_service::loading() const noexcept {
  return pending_ != nullptr || completed_.has_value();
}
scene_retirement_statistics scene_service::retirement() const noexcept {
  auto value = retirement_;
  value.pending = retired_ != nullptr;
  return value;
}
gneiss::engine::world* scene_service::native_world() const noexcept {
  return active_ ? active_->native_world() : nullptr;
}

gneiss::engine::scene_instances* scene_service::scenes() noexcept {
  return active_ ? active_->scenes() : nullptr;
}
bool scene_service::can_activate() const noexcept {
  return !stopping_ && !reload_ && !retired_ && loading_ && !loading_->busy();
}
engine::error scene_service::request_reload(scene_instance_id instance, std::string_view uri,
                                            bool prefab) {
  check_owner();
  if (stopping_ || !active_ || !loading_ || !resources_)
    return engine::error::invalid_state;
  if (reload_ || loading() || visual_loading() || loading_->busy() || retired_)
    return engine::error::not_ready;
  auto valid_value = scenes()->get_node_count(instance);
  if (!valid_value)
    return valid_value.error();

  auto next = std::make_unique<reload_task>();
  next->instance = instance;
  next->uri = uri;
  next->prefab = prefab;
  if (prefab) {
    auto loaded = resources_->load<engine::prefab>(uri, true);
    if (!loaded)
      return loaded.error();
    next->descriptions = std::move(*loaded);
  } else {
    auto loaded = resources_->load<engine::scene>(uri, true);
    if (!loaded)
      return loaded.error();
    next->descriptions = std::move(*loaded);
  }
  reload_ = std::move(next);
  return engine::error::success;
}

void scene_service::advance_reload() try {
  if (!reload_ || reload_->result != engine::error::not_ready)
    return;
  auto& value = *reload_;
  auto prepared = std::visit(
      [](auto& request)
          -> engine::result<engine::scene_instantiation_data, engine::scene_diagnostic> {
        auto loaded = request.take();
        if (!loaded) {
          auto failure = std::move(loaded).error();
          return engine::scene_diagnostic{failure.error, failure.byte_offset,
                                          std::move(failure.path), std::move(failure.message)};
        }
        return engine::make_instantiation_data(*loaded, request.source_bytes());
      },
      value.descriptions);
  if (!prepared) {
    if (prepared.error().error != engine::error::not_ready)
      value.result = prepared.error().error;
    return;
  }
  value.prepared = std::move(*prepared);
  // 结构仅需要稳定资源身份，不等待视觉内容加载。
  for (const auto& source : value.prepared.assets) {
    auto reserved = resources_->reserve_resource(source);
    if (!reserved) {
      value.result = reserved.error();
      return;
    }
    value.retained.push_back(std::move(*reserved));
  }
  value.result =
      value.prefab ? scenes()->reload_prefab_loaded(value.instance, value.uri,
                                                    value.prepared.prefabs.at(value.uri).resource)
                   : scenes()->reload_loaded(value.instance, std::move(value.prepared.description));
  if (value.result.ok())
    visuals_.begin(std::move(value.prepared.assets), 1, 1);

} catch (const std::bad_alloc&) {
  if (reload_)
    reload_->result = engine::error::out_of_memory;
} catch (...) {
  if (reload_)
    reload_->result = engine::error::internal;
}

engine::result<std::optional<scene_reload_completion>> scene_service::poll_reload() {
  check_owner();
  if (!reload_)
    return engine::error::invalid_state;
  if (reload_->result == engine::error::not_ready)
    return std::optional<scene_reload_completion>{};
  scene_reload_completion completed{reload_->result};
  reload_.reset();
  return std::optional{completed};
}

} // namespace gneiss::runtime
