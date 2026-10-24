// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/loading/scene_service.hpp"
#include "engine/core/diagnostics/loop_timing.hpp"
#include "engine/resource/asset_manager.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <map>
#include <utility>

namespace gneiss::runtime {

gneiss_result scene_service::initialize(engine::resource::asset_manager& assets,
                                        engine::render::render_asset_loader& render) {
  if (active_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  auto next = std::make_unique<scene_domain>();
  const auto result = next->initialize(assets, render);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  asset_owner_ = &assets;
  render_assets_ = &render;
  active_ = std::move(next);
  return GNEISS_SUCCESS;
}

gneiss_result
scene_service::attach_executor(engine::core::tasks::task_executor& executor,
                               const engine::resource::virtual_file_system& files,
                               engine::render::render_resource_service& resources,
                               engine::render::texture_upload_backend backend,
                               std::shared_ptr<engine::core::progress_notification> notification) {
  if (textures_ || !active_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  // 先构造可失败依赖，再发布装配状态，失败时不留下半装配服务。
  auto mounted_files = files;
  const auto profile = backend.profile;
  auto textures = std::make_unique<engine::render::texture_load_service>(
      executor, files, *render_assets_, std::move(backend), notification,
      asset_owner_->documents());
  const auto scope = executor.make_scope();
  if (scope.id == 0U) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  files_ = std::move(mounted_files);
  executor_ = &executor;
  scope_ = scope;
  resources_ = &resources;
  notification_ = std::move(notification);
  render_assets_->set_default_profile(profile);
  textures_ = std::move(textures);
  return GNEISS_SUCCESS;
}

void scene_service::advance() {
  if (retired_) {
    const auto start = std::chrono::steady_clock::now();
    const engine::core::diagnostics::loop_span span(
        engine::core::diagnostics::loop_stage::retirement);
    retired_.reset();
    render_assets_->release_unused();
    asset_owner_->collect_unused();
    retirement_.last_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    ++retirement_.retired_domains;
  }
  advance_visuals();
  advance_load();
}

void scene_service::advance_visuals() {
  if (!textures_ || visual_completion_) {
    return;
  }
  if (visual_pending_) {
    textures_->advance();
    engine::render::texture_load_completion completion;
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
  if (submitted == GNEISS_ERROR_NOT_READY) {
    return;
  }
  if (submitted == GNEISS_SUCCESS) {
    visual_pending_ = true;
  } else {
    engine::render::texture_load_completion completion;
    completion.result = submitted;
    completion.results.push_back({.source = visual_queue_[visual_cursor_], .result = submitted});
    visual_completion_ = std::move(completion);
  }
  ++visual_cursor_;
}

bool scene_service::take_visual_completion(engine::render::texture_load_completion& output) {
  if (!visual_completion_) {
    return false;
  }
  output = std::move(*visual_completion_);
  visual_completion_.reset();
  return true;
}

void scene_service::shutdown() noexcept {
  visual_queue_.clear();
  visual_completion_.reset();
  visual_pending_ = false;
  stop_load();
  textures_.reset();
  retired_.reset();
  active_.reset();
  if (asset_owner_ != nullptr) {
    render_assets_->release_unused();
    asset_owner_->collect_unused();
  }
  asset_owner_ = nullptr;
  render_assets_ = nullptr;
}

bool scene_service::can_start_load() const noexcept {
  return !retired_ && textures_ && (!textures_->busy() || visual_pending_);
}
bool scene_service::loading() const noexcept {
  return pending_ != nullptr || completed_.has_value();
}
scene_retirement_statistics scene_service::retirement() const noexcept {
  auto value = retirement_;
  value.pending = retired_ != nullptr;
  return value;
}
engine::world::world* scene_service::native_world() const noexcept {
  return active_ ? active_->native_world() : nullptr;
}

gneiss_world scene_service::world() const noexcept {
  return active_ ? active_->world() : GNEISS_NULL_WORLD;
}
engine::scene::scene_instances* scene_service::scenes() noexcept {
  return active_ ? active_->scenes() : nullptr;
}
engine::render::texture_load_service* scene_service::textures() noexcept { return textures_.get(); }
gneiss_result
scene_service::request_assets(std::span<const engine::render::render_asset_reload> sources,
                              std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                              bool reload) {
  request = 0U;
  if (!textures_ || loading() || visual_loading() || textures_->busy()) {
    return GNEISS_ERROR_NOT_READY;
  }
  return textures_->submit_assets(sources, session, revision, request, reload);
}

gneiss_result scene_service::poll_assets(engine::render::texture_load_completion& completion,
                                         bool& ready) {
  ready = false;
  if (!textures_ || visual_loading()) {
    return GNEISS_ERROR_NOT_READY;
  }
  textures_->advance();
  ready = textures_->take(completion);
  return GNEISS_SUCCESS;
}

} // namespace gneiss::runtime
