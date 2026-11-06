// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/asset/detail/loader_factory.hpp"
#include "engine/resource/scene/detail/scene_asset_access.hpp"
namespace gneiss::engine::detail {
namespace {
template <class T, class D> struct scene_loader final : asset_loader {
  asset_identity source;
  bool reload{};
  ref<scene_content> content;
  std::map<std::string, asset_request<prefab>, std::less<>> pending;
  bool dependencies_started{};
  explicit scene_loader(asset_identity identity, bool should_reload)
      : asset_loader(identity.uri), source(std::move(identity)), reload(should_reload) {}
  void decode(const std::vector<std::byte>& bytes) override {
    auto decoded = asset_file::deserialize_ref<D>(std::span<const std::byte>{bytes});
    status = decoded ? error::success : decoded.error().error;
    if (decoded) {
      if constexpr (std::is_same_v<T, scene>) {
        content = make_ref<scene_content>();
        content->description = std::move(*decoded);
        value = scene_asset_access::make<T>(source, ref<const scene_content>{content});
      } else {
        value = scene_asset_access::make<T>(source, std::move(*decoded));
      }
    } else {
      diagnostic = std::move(decoded).error();
    }
    metrics.bytes = input_bytes;
  }
  error resolve_dependencies(asset_manager& assets) override {
    if constexpr (std::is_same_v<T, scene>) {
      if (!dependencies_started) {
        for (const auto& object : content->description->objects) {
          if (!object.prefab || pending.contains(object.prefab->uri))
            continue;
          auto request = assets.load<prefab>(object.prefab->uri, reload);
          if (!request) {
            reject({request.error(), 0, object.prefab->uri, "无法加载场景 Prefab 依赖"});
            return status;
          }
          pending.emplace(object.prefab->uri, std::move(*request));
        }
        dependencies_started = true;
      }
      for (auto it = pending.begin(); it != pending.end();) {
        auto loaded = it->second.take();
        if (!loaded) {
          if (loaded.error().error == error::not_ready) {
            ++it;
            continue;
          }
          auto failure = std::move(loaded).error();
          failure.path = it->first + (failure.path.empty() ? "" : ":" + failure.path);
          reject(std::move(failure));
          return status;
        }
        content->prefabs.emplace(it->first, scene_prefab_dependency{*loaded, (*loaded)->snapshot(),
                                                                    it->second.source_bytes()});
        it = pending.erase(it);
      }
      if (!pending.empty())
        return error::not_ready;
      // 成功后只留下不可变快照；Loader 不再保留可变入口。
      content.reset();
    }
    return error::success;
  }
  void adopt(const std::shared_ptr<const void>& previous) noexcept override {
    if (!previous)
      return;
    auto target = std::static_pointer_cast<const T>(previous);
    scene_asset_access::adopt(*target, *std::static_pointer_cast<const T>(value));
    value = std::move(target);
  }
};
}
std::shared_ptr<asset_loader> make_scene_loader(asset_identity source, bool is_prefab,
                                                bool reload) {
  if (is_prefab)
    return std::make_shared<scene_loader<prefab, prefab_description>>(std::move(source), reload);
  return std::make_shared<scene_loader<scene, scene_description>>(std::move(source), reload);
}
}
