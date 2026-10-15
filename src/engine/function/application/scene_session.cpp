// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/application/scene_session.hpp"
#include "engine/core/diagnostics/loop_timing.hpp"

#include <chrono>
#include <utility>

namespace gneiss::application_internal {

scene_session::~scene_session() noexcept { shutdown(); }

gneiss_result scene_session::initialize(const asset_internal::virtual_file_system& files,
                                        render_internal::render_resource_service& resources) {
  if (active_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  active_ = std::make_unique<application_scene_state>(files, resources);
  return active_->initialize();
}

gneiss_result
scene_session::attach_executor(tasks::task_executor& executor,
                               const asset_internal::virtual_file_system& files,
                               render_internal::render_resource_service& resources,
                               render_internal::texture_upload_backend backend,
                               std::shared_ptr<core::progress_notification> notification) {
  if (textures_ || !active_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  // 两个服务都成功构造后才发布，异常不留下半装配状态。
  auto loader =
      std::make_unique<scene_load_service>(executor, files, resources, backend, notification);
  auto textures = std::make_unique<render_internal::texture_load_service>(
      executor, files, active_->assets, std::move(backend), std::move(notification));
  loader_ = std::move(loader);
  textures_ = std::move(textures);
  return GNEISS_SUCCESS;
}

void scene_session::advance() {
  if (retired_) {
    const auto start = std::chrono::steady_clock::now();
    const diagnostics::loop_span span(diagnostics::loop_stage::retirement);
    retired_.reset();
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
  std::unique_ptr<render_internal::texture_load_service> next_assets;
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
gneiss_world scene_session::world() const noexcept {
  return active_ ? active_->world : GNEISS_NULL_WORLD;
}
scene_internal::scene_instance_service* scene_session::scenes() noexcept {
  return active_ ? active_->scenes.get() : nullptr;
}
render_internal::render_asset_loader& scene_session::assets() noexcept { return active_->assets; }
render_internal::texture_load_service* scene_session::textures() noexcept {
  return textures_.get();
}
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
gneiss_result scene_session::cancel_scene(std::uint64_t request) {
  return loader_ && loader_->cancel(request) ? GNEISS_SUCCESS : GNEISS_ERROR_NOT_READY;
}

} // namespace gneiss::application_internal
