// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/application/scene_session.hpp"
#include "engine/core/diagnostics/loop_timing.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <map>
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
  previous_assets_.clear();
  asset_request_ = 0U;
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
gneiss_result
scene_session::request_assets(std::span<const render_internal::render_asset_reload> sources,
                              std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                              bool reload) {
  request = 0U;
  if (!textures_ || loading() || textures_->busy()) {
    return GNEISS_ERROR_NOT_READY;
  }
  if (!reload) {
    return textures_->submit_assets(sources, session, revision, request, false);
  }
  using type = render_internal::render_asset_type;
  using loader = render_internal::render_asset_loader;
  std::map<std::string, type, std::less<>> requested;
  if (sources.empty() || sources.size() > render_internal::texture_load_service::maximum_assets) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  for (const auto& source : sources) {
    if (!requested.emplace(source.uri, source.type).second) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  std::vector<render_internal::render_asset_lease> previous;
  const auto captured = active_->scenes->capture_render_assets(previous);
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
  std::vector<render_internal::render_asset_reload> expanded;
  expanded.reserve(requested.size());
  for (const auto& [uri, asset_type] : requested) {
    expanded.push_back({.uri = uri, .type = asset_type});
  }
  const auto result =
      textures_->submit_assets(expanded, session, revision, request, true,
                               render_internal::texture_load_service::maximum_candidate_bytes,
                               loader::publication_mode::fixed_version);
  if (result == GNEISS_SUCCESS) {
    previous_assets_ = std::move(previous);
    asset_request_ = request;
  }
  return result;
}

gneiss_result scene_session::poll_assets(render_internal::texture_load_completion& completion,
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
      std::vector<scene_internal::scene_instance_service::asset_replacement> changes;
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
          std::vector<asset_internal::resource_cache::version_selection> selections;
          selections.reserve(completion.versions.size());
          std::ranges::transform(
              completion.versions, std::back_inserter(selections), [&](const auto& version) {
                return asset_internal::resource_cache::version_selection{
                    .selection = render_internal::render_asset_loader::selection_key(
                        version.identity, completion.profile),
                    .version = version,
                };
              });
          completion.result = active_->cache.adopt_versions(
              selections, [&] { return active_->scenes->replace_render_assets(changes); });
        }
      }
    } catch (const std::bad_alloc&) {
      completion.result = GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      completion.result = GNEISS_ERROR_INTERNAL;
    }
  }
  if (completion.result != GNEISS_SUCCESS) {
    if (completion.state != render_internal::texture_load_state::cancelled) {
      completion.state = render_internal::texture_load_state::failed;
    }
    completion.assets.clear();
    completion.textures.clear();
    completion.versions.clear();
  }
  previous_assets_.clear();
  asset_request_ = 0U;
  active_->assets.release_unused();
  completion.commit_ms +=
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - commit_started)
          .count();
  return GNEISS_SUCCESS;
}

gneiss_result scene_session::cancel_scene(std::uint64_t request) {
  return loader_ && loader_->cancel(request) ? GNEISS_SUCCESS : GNEISS_ERROR_NOT_READY;
}

} // namespace gneiss::application_internal
