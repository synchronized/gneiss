// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/loading/scene_session.hpp"
#include "engine/core/diagnostics/loop_timing.hpp"
#include "engine/resource/asset_manager.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <map>
#include <utility>

namespace gneiss::runtime {

scene_session::~scene_session() noexcept { shutdown(); }

gneiss_result scene_session::initialize(engine::resource::asset_manager& assets,
                                        engine::render::render_asset_loader& render) {
  if (active_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  auto next = std::make_unique<application_scene_state>();
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
scene_session::attach_executor(engine::core::tasks::task_executor& executor,
                               const engine::resource::virtual_file_system& files,
                               engine::render::render_resource_service& resources,
                               engine::render::texture_upload_backend backend,
                               std::shared_ptr<engine::core::progress_notification> notification) {
  if (textures_ || !active_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  // 两个服务都成功构造后才发布，异常不留下半装配状态。
  auto loader = std::make_unique<scene_load_service>(executor, files, resources, *asset_owner_,
                                                     *render_assets_, backend, notification);
  const auto profile = backend.profile;
  auto textures = std::make_unique<engine::render::texture_load_service>(
      executor, files, *render_assets_, std::move(backend), std::move(notification),
      asset_owner_->documents());
  render_assets_->set_default_profile(profile);
  loader_ = std::move(loader);
  textures_ = std::move(textures);
  return GNEISS_SUCCESS;
}

void scene_session::advance() {
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
  if (loader_) {
    loader_->advance();
  }
}

gneiss_result scene_session::activate(std::uint64_t request, scene_load_completion& completion) {
  if (!loader_ || retired_ || (textures_ && textures_->busy())) {
    return GNEISS_ERROR_NOT_READY;
  }
  const auto start = std::chrono::steady_clock::now();
  std::unique_ptr<engine::render::texture_load_service> next_assets;
  auto next = loader_->take_candidate(request, completion, next_assets);
  if (!next) {
    return GNEISS_ERROR_NOT_READY;
  }
  retired_ = std::move(active_);
  active_ = std::move(next);
  textures_ = std::move(next_assets);
  completion.activation_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  return GNEISS_SUCCESS;
}

void scene_session::shutdown() noexcept {
  loader_.reset();
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

bool scene_session::can_start_load() const noexcept {
  return !retired_ && textures_ && !textures_->busy();
}
bool scene_session::loading() const noexcept { return loader_ && loader_->busy(); }
scene_retirement_statistics scene_session::retirement() const noexcept {
  auto value = retirement_;
  value.pending = retired_ != nullptr;
  return value;
}
engine::world::world* scene_session::native_world() const noexcept {
  return active_ ? active_->native_world() : nullptr;
}

gneiss_world scene_session::world() const noexcept {
  return active_ ? active_->world() : GNEISS_NULL_WORLD;
}
engine::scene::scene_instance_service* scene_session::scenes() noexcept {
  return active_ ? active_->scenes() : nullptr;
}
engine::render::texture_load_service* scene_session::textures() noexcept { return textures_.get(); }
gneiss_result scene_session::request_scene(std::string_view uri, std::uint64_t session,
                                           std::uint64_t revision, std::uint64_t& request) {
  request = 0U;
  if (!loader_ || !can_start_load()) {
    return GNEISS_ERROR_NOT_READY;
  }
  return loader_->submit(uri, session, revision, request);
}
bool scene_session::scene_progress(scene_load_progress& progress) const {
  return loader_ && loader_->progress(progress);
}
gneiss_result scene_session::poll_scene(scene_load_completion& completion, bool& ready) {
  ready = false;
  if (!loader_) {
    return GNEISS_ERROR_NOT_READY;
  }
  advance();
  ready = loader_->take(completion);
  return GNEISS_SUCCESS;
}
gneiss_result
scene_session::request_assets(std::span<const engine::render::render_asset_reload> sources,
                              std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                              bool reload) {
  request = 0U;
  if (!textures_ || loading() || textures_->busy()) {
    return GNEISS_ERROR_NOT_READY;
  }
  return textures_->submit_assets(sources, session, revision, request, reload);
}

gneiss_result scene_session::poll_assets(engine::render::texture_load_completion& completion,
                                         bool& ready) {
  ready = false;
  if (!textures_) {
    return GNEISS_ERROR_NOT_READY;
  }
  textures_->advance();
  ready = textures_->take(completion);
  return GNEISS_SUCCESS;
}

gneiss_result scene_session::cancel_scene(std::uint64_t request) {
  return loader_ && loader_->cancel(request) ? GNEISS_SUCCESS : GNEISS_ERROR_NOT_READY;
}

} // namespace gneiss::runtime
