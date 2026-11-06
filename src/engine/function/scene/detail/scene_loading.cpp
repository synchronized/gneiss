// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "scene_loading.hpp"
#include "engine/resource/asset_uri.hpp"
#include "engine/resource/prefab.hpp"
#include "engine/resource/scene.hpp"
#include <algorithm>
#include <chrono>
namespace gneiss::engine::detail {
struct scene_description_request::state {
  asset_manager& assets;
  std::string uri;
  bool prefab{}, started{}, reload{};
  scene_prepare_limits limits;
  asset_request<engine::scene> scene;
  asset_request<engine::prefab> current;
  scene_preparation_inputs inputs;
  std::vector<std::string> uris;
  std::size_t cursor{}, bytes{};
};
scene_description_request::scene_description_request(asset_manager& assets, std::string uri,
                                                     bool prefab, scene_prepare_limits limits,
                                                     bool reload)
    : state_(std::make_unique<state>(state{.assets = assets,
                                           .uri = std::move(uri),
                                           .prefab = prefab,
                                           .reload = reload,
                                           .limits = limits,
                                           .scene = {},
                                           .current = {},
                                           .inputs = {},
                                           .uris = {}})) {}
scene_description_request::~scene_description_request() = default;
error scene_description_request::advance(scene_preparation_inputs& output,
                                         scene_diagnostic& diagnostic) {
  auto& s = *state_;
  const auto fail = [&](error status) {
    diagnostic = {status, 0, {}, {}};
    return status;
  };
  if (!s.started) {
    if (s.prefab) {
      s.uris.push_back(s.uri);
    } else {
      auto loaded = s.assets.load<engine::scene>(s.uri, s.reload);
      if (!loaded)
        return fail(loaded.error());
      s.scene = std::move(*loaded);
    }
    s.started = true;
  }
  if (s.scene) {
    auto loaded = s.scene.take();
    if (!loaded) {
      auto failure = std::move(loaded).error();
      diagnostic = {failure.error, failure.byte_offset, std::move(failure.path),
                    std::move(failure.message)};
      return diagnostic.error;
    }
    s.inputs.source_asset = *loaded;
    s.inputs.scene = (*loaded)->snapshot();
    s.bytes = s.scene.source_bytes();
    for (const auto& instance : s.inputs.scene->objects)
      if (instance.prefab && std::ranges::find(s.uris, instance.prefab->uri) == s.uris.end())
        s.uris.push_back(instance.prefab->uri);
  }
  if (s.bytes > s.limits.source_bytes || s.uris.size() > s.limits.dependencies)
    return fail(error::invalid_argument);
  if (s.cursor < s.uris.size()) {
    const auto& uri = s.uris[s.cursor];
    if (!s.current) {
      auto loaded = s.assets.load<engine::prefab>(uri, s.reload);
      if (!loaded)
        return fail(loaded.error());
      s.current = std::move(*loaded);
    }
    auto loaded = s.current.take();
    if (!loaded) {
      auto failure = std::move(loaded).error();
      diagnostic = {failure.error, failure.byte_offset, std::move(failure.path),
                    std::move(failure.message)};
      return diagnostic.error;
    }
    auto value = (*loaded)->snapshot();
    if (s.current.source_bytes() > s.limits.source_bytes - s.bytes)
      return fail(error::invalid_argument);
    s.bytes += s.current.source_bytes();
    s.inputs.prefab_assets.emplace(uri, *loaded);
    s.inputs.prefabs.emplace(uri, std::move(value));
    ++s.cursor;
    if (s.cursor < s.uris.size())
      return error::not_ready;
  }
  s.inputs.source_bytes = s.bytes;
  output = std::move(s.inputs);
  diagnostic = {};
  return error::success;
}
scene_preparation_operation::scene_preparation_operation(asset_manager& assets, std::string uri,
                                                         bool prefab, bool reload)
    : descriptions_(std::make_unique<scene_description_request>(assets, uri, prefab,
                                                                scene_prepare_limits{}, reload)),
      prefab_uri_(prefab ? std::move(uri) : std::string{}) {}
void scene_preparation_operation::cancel() noexcept {
  if (completed_)
    return;
  descriptions_.reset();
  status_ = error::invalid_state;
  diagnostic_ = {status_, 0, {}, {}};
  completed_ = true;
}
result<std::shared_ptr<const scene_instantiation_data>, scene_diagnostic>
scene_preparation_operation::advance() try {
  if (!completed_) {
    status_ = descriptions_->advance(inputs_, diagnostic_);
    if (status_ == error::not_ready)
      return scene_diagnostic{error::not_ready, 0, {}, {}};
    descriptions_.reset();
    if (status_.ok()) {
      const auto started = std::chrono::steady_clock::now();
      // 必要依赖已加载；所属线程一次完成层级整理，不再提交独立 CPU 准备任务。
      status_ = prepare_loaded_scene(inputs_, prefab_uri_, *prepared_, diagnostic_, {});
      metrics_.decode_ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
              .count();
    }
    completed_ = true;
  }
  if (status_.failed()) {
    diagnostic_.error = status_;
    return diagnostic_;
  }
  return std::shared_ptr<const scene_instantiation_data>{prepared_};
} catch (const std::bad_alloc&) {
  cancel();
  status_ = error::out_of_memory;
  diagnostic_.error = status_;
  return diagnostic_;
} catch (...) {
  cancel();
  status_ = error::internal;
  diagnostic_.error = status_;
  return diagnostic_;
}
}

namespace gneiss::engine {
result<asset_request<scene_instantiation_data>>
prepare_scene(asset_manager& assets, std::string_view uri, bool reload) noexcept try {
  if (validate_uri(uri).failed())
    return error::invalid_argument;
  return assets.enqueue_scene_preparation(std::make_shared<detail::scene_preparation_operation>(
      assets, std::string(uri), false, reload));
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
result<asset_request<scene_instantiation_data>>
prepare_prefab(asset_manager& assets, std::string_view uri, bool reload) noexcept try {
  if (validate_uri(uri).failed())
    return error::invalid_argument;
  return assets.enqueue_scene_preparation(std::make_shared<detail::scene_preparation_operation>(
      assets, std::string(uri), true, reload));
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
} // namespace gneiss::engine
