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
scene_session::attach_executor(tasks::task_executor& executor,
                               const engine::resource::virtual_file_system& files,
                               engine::render::render_resource_service& resources,
                               engine::render::texture_upload_backend backend,
                               std::shared_ptr<core::progress_notification> notification) {
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
    const diagnostics::loop_span span(diagnostics::loop_stage::retirement);
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
  previous_assets_.clear();
  asset_request_ = 0U;
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
  if (!reload) {
    return textures_->submit_assets(sources, session, revision, request, false);
  }
  using type = engine::render::render_asset_type;
  using loader = engine::render::render_asset_loader;
  std::map<std::string, type, std::less<>> requested;
  if (sources.empty() || sources.size() > engine::render::texture_load_service::maximum_assets) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  for (const auto& source : sources) {
    if (!requested.emplace(source.uri, source.type).second) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  std::vector<engine::render::render_asset_lease> previous;
  const auto captured = active_->scenes()->capture_render_assets(previous);
  if (captured != GNEISS_SUCCESS) {
    return captured;
  }
  // 纹理更新必须重建实际引用它的材质，不能只发布一个无人使用的新纹理。
  for (const auto& lease : previous) {
    if (lease.type() != type::material) {
      continue;
    }
    const auto dependencies = loader::texture_dependencies(lease);
    const auto affected = std::ranges::any_of(dependencies, [&](const auto& dependency) {
      const auto found = requested.find(dependency.uri());
      return dependency && found != requested.end() && found->second == type::texture;
    });
    if (!affected) {
      continue;
    }
    const auto [material, inserted] = requested.emplace(lease.uri(), type::material);
    if (!inserted && material->second != type::material) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  std::erase_if(previous, [&](const auto& lease) {
    const auto found = requested.find(lease.uri());
    return found == requested.end() || found->second != lease.type();
  });
  std::vector<engine::render::render_asset_reload> expanded;
  expanded.reserve(requested.size());
  for (const auto& [uri, asset_type] : requested) {
    expanded.push_back({.uri = uri, .type = asset_type});
  }
  const auto result =
      textures_->submit_assets(expanded, session, revision, request, true,
                               engine::render::texture_load_service::maximum_candidate_bytes,
                               loader::publication_mode::fixed_version);
  if (result == GNEISS_SUCCESS) {
    previous_assets_ = std::move(previous);
    asset_request_ = request;
  }
  return result;
}

gneiss_result scene_session::poll_assets(engine::render::texture_load_completion& completion,
                                         bool& ready) {
  ready = false;
  if (!textures_) {
    return GNEISS_ERROR_NOT_READY;
  }
  textures_->advance();
  ready = textures_->take(completion);
  if (!ready || asset_request_ == 0U) {
    return GNEISS_SUCCESS;
  }
  const auto commit_started = std::chrono::steady_clock::now();
  if (completion.result == GNEISS_SUCCESS) {
    try {
      std::vector<engine::scene::scene_instance_service::asset_replacement> changes;
      if (completion.request != asset_request_) {
        completion.result = GNEISS_ERROR_INVALID_STATE;
      } else {
        changes.reserve(previous_assets_.size());
        for (const auto& previous : previous_assets_) {
          const auto next = std::ranges::find_if(completion.assets, [&](const auto& lease) {
            return lease.type() == previous.type() && lease.uri() == previous.uri();
          });
          if (next == completion.assets.end()) {
            completion.result = GNEISS_ERROR_INVALID_STATE;
            break;
          }
          changes.push_back({.previous = previous, .replacement = *next});
        }
        if (completion.result == GNEISS_SUCCESS) {
          completion.result = accept_edit_assets(
              completion, [&] { return active_->scenes()->replace_render_assets(changes); });
        }
      }
    } catch (const std::bad_alloc&) {
      completion.result = GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      completion.result = GNEISS_ERROR_INTERNAL;
    }
  }
  if (completion.result != GNEISS_SUCCESS) {
    if (completion.state != engine::render::texture_load_state::cancelled) {
      completion.state = engine::render::texture_load_state::failed;
    }
    completion.assets.clear();
    completion.textures.clear();
    completion.versions.clear();
  }
  previous_assets_.clear();
  asset_request_ = 0U;
  render_assets_->release_unused();
  asset_owner_->collect_unused();
  completion.commit_ms +=
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - commit_started)
          .count();
  return GNEISS_SUCCESS;
}

gneiss_result
scene_session::accept_edit_assets(const engine::render::texture_load_completion& completion,
                                  const std::function<gneiss_result()>& accept) {
  if (!active_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  if (!accept || completion.result != GNEISS_SUCCESS) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    std::unique_ptr<engine::resource::resource_cache::prepared_adoption> prepared;
    const auto result = render_assets_->prepare_adoption(completion.assets, completion.versions,
                                                         completion.profile, prepared);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    // 场景修改属于应用流程；失败或异常时释放准备状态，保留默认版本。
    const auto applied = accept();
    if (applied == GNEISS_SUCCESS) {
      prepared->commit();
    }
    return applied;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_session::cancel_scene(std::uint64_t request) {
  return loader_ && loader_->cancel(request) ? GNEISS_SUCCESS : GNEISS_ERROR_NOT_READY;
}

} // namespace gneiss::runtime
