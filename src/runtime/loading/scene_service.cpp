// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/loading/scene_service.hpp"
#include "engine/core/diagnostics/loop_timing.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/asset_manager.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <map>
#include <utility>

namespace gneiss::runtime {

gneiss::engine::result scene_service::initialize(gneiss::engine::asset_manager& assets,
                                                 gneiss::engine::render_asset_loader& render) {
  if (active_ || stopping_) {
    return gneiss::engine::result::invalid_state;
  }
  auto next = std::make_unique<scene_domain>();
  const auto result = next->initialize(render);
  if (result != gneiss::engine::result::success) {
    return result;
  }
  asset_owner_ = &assets;
  render_assets_ = &render;
  active_ = std::move(next);
  return gneiss::engine::result::success;
}

gneiss::engine::result scene_service::attach_executor(
    gneiss::engine::task_executor& executor, gneiss::engine::render_resource_service& resources,
    gneiss::engine::texture_prepare_profile profile,
    std::shared_ptr<gneiss::engine::progress_notification> notification) {
  if (textures_ || !active_) {
    return gneiss::engine::result::invalid_state;
  }
  // 先构造可失败依赖，再发布装配状态，失败时不留下半装配服务。
  auto textures = std::make_unique<gneiss::engine::texture_load_service>(executor, *render_assets_,
                                                                         profile, notification);
  const auto scope = executor.make_scope();
  if (scope.id == 0U) {
    return gneiss::engine::result::invalid_state;
  }
  executor_ = &executor;
  scope_ = scope;
  resources_ = &resources;
  notification_ = std::move(notification);
  render_assets_->set_default_profile(profile);
  textures_ = std::move(textures);
  return gneiss::engine::result::success;
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
    asset_owner_->collect_unused();
    retirement_.last_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    ++retirement_.retired_domains;
  }
  advance_visuals();
  advance_load();
  advance_reload();
}

void scene_service::advance_visuals() {
  if (!textures_ || visual_completion_) {
    return;
  }
  if (visual_pending_) {
    textures_->advance();
    gneiss::engine::texture_load_completion completion;
    if (!textures_->take(completion)) {
      return;
    }
    visual_pending_ = false;
    visual_completion_ = std::move(completion);
    return;
  }
  if (visual_cursor_ == visual_queue_.size()) {
    visual_queue_.clear();
    visual_cursor_ = 0U;
    return;
  }
  std::uint64_t request{};
  const auto submitted =
      textures_->submit_assets(std::span(visual_queue_).subspan(visual_cursor_, 1U),
                               visual_session_, visual_revision_, request, false);
  if (submitted == gneiss::engine::result::not_ready) {
    return;
  }
  if (submitted == gneiss::engine::result::success) {
    visual_pending_ = true;
  } else {
    gneiss::engine::texture_load_completion completion;
    completion.result = submitted;
    completion.results.push_back({.source = visual_queue_[visual_cursor_], .result = submitted});
    visual_completion_ = std::move(completion);
  }
  ++visual_cursor_;
}

bool scene_service::take_visual_completion(gneiss::engine::texture_load_completion& output) {
  if (!visual_completion_) {
    return false;
  }
  output = std::move(*visual_completion_);
  visual_completion_.reset();
  return true;
}

void scene_service::shutdown() noexcept {
  stopping_ = true;
  visual_queue_.clear();
  visual_completion_.reset();
  visual_pending_ = false;
  reload_.reset();
  stop_load();
  textures_.reset();
  retired_.reset();
  active_.reset();
  if (asset_owner_ != nullptr) {
    asset_owner_->collect_unused();
  }
  asset_owner_ = nullptr;
  render_assets_ = nullptr;
}

bool scene_service::can_start_load() const noexcept {
  return !stopping_ && !reload_ && !retired_ && textures_ &&
         (!textures_->busy() || visual_pending_);
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
gneiss::engine::texture_load_service* scene_service::textures() noexcept { return textures_.get(); }
gneiss::engine::result
scene_service::request_assets(std::span<const gneiss::engine::render_asset_reload> sources,
                              std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                              bool reload) {
  request = 0U;
  if (stopping_)
    return gneiss::engine::result::invalid_state;
  if (!textures_ || reload_ || loading() || visual_loading() || textures_->busy()) {
    return gneiss::engine::result::not_ready;
  }
  return textures_->submit_assets(sources, session, revision, request, reload);
}

gneiss::engine::result
scene_service::poll_assets(gneiss::engine::texture_load_completion& completion, bool& ready) {
  ready = false;
  if (!textures_ || reload_ || visual_loading()) {
    return gneiss::engine::result::not_ready;
  }
  textures_->advance();
  ready = textures_->take(completion);
  return gneiss::engine::result::success;
}

engine::result scene_service::request_reload(scene_instance_id instance, std::string_view uri,
                                             bool prefab) {
  check_owner();
  if (stopping_ || !active_ || !textures_ || !asset_owner_)
    return engine::result::invalid_state;
  if (reload_ || loading() || visual_loading() || textures_->busy() || retired_)
    return engine::result::not_ready;
  std::uint64_t count{};
  const auto valid = scenes()->get_node_count(instance, &count);
  if (valid.failed())
    return valid;
  auto next = std::make_unique<reload_task>();
  next->instance = instance;
  next->uri = uri;
  next->prefab = prefab;
  next->descriptions =
      std::make_unique<engine::scene_description_request>(*asset_owner_, next->uri, prefab);
  reload_ = std::move(next);
  return engine::result::success;
}

void scene_service::advance_reload() try {
  if (!reload_ || reload_->result != engine::result::not_ready)
    return;
  auto& value = *reload_;
  if (value.descriptions) {
    engine::scene_diagnostic diagnostic;
    auto status = value.descriptions->advance(value.inputs, diagnostic);
    if (status == engine::result::not_ready)
      return;
    if (status.ok())
      status =
          engine::prepare_loaded_scene(value.inputs, value.prefab ? value.uri : std::string_view{},
                                       value.prepared, diagnostic, {});
    value.descriptions.reset();
    if (status.failed()) {
      value.result = status;
      return;
    }
  }
  if (value.asset_pending) {
    textures_->advance();
    engine::texture_load_completion completion;
    if (!textures_->take(completion))
      return;
    value.asset_pending = false;
    if (completion.result.failed()) {
      value.result = completion.result;
      return;
    }
    for (auto& asset : completion.assets)
      value.retained.push_back(std::move(asset));
    ++value.cursor;
  }
  if (value.cursor < value.prepared.assets.size()) {
    std::uint64_t request{};
    const auto status = textures_->submit_assets(
        std::span(value.prepared.assets).subspan(value.cursor, 1), 1, 1, request, false);
    if (status.ok())
      value.asset_pending = true;
    else if (status != engine::result::not_ready)
      value.result = status;
    return;
  }
  value.result =
      value.prefab ? scenes()->reload_prefab_loaded(value.instance, value.uri,
                                                    value.inputs.prefabs.at(value.uri))
                   : scenes()->reload_loaded(value.instance, std::move(value.prepared.description));
} catch (const std::bad_alloc&) {
  if (reload_)
    reload_->result = engine::result::out_of_memory;
} catch (...) {
  if (reload_)
    reload_->result = engine::result::internal;
}

engine::result scene_service::poll_reload(engine::result& result, bool& ready) {
  check_owner();
  ready = false;
  if (!reload_)
    return engine::result::invalid_state;
  if (reload_->result == engine::result::not_ready)
    return engine::result::success;
  result = reload_->result;
  reload_.reset();
  ready = true;
  return engine::result::success;
}

} // namespace gneiss::runtime
