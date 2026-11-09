// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/asset/detail/content_dependency.hpp"
#include "engine/function/asset/detail/loader_factory.hpp"
#include "engine/resource/scene/detail/scene_asset_access.hpp"
namespace gneiss::engine::detail {
namespace {
template <class T, class D> struct scene_loader final : asset_loader {
  asset_identity source;
  bool reload{};
  ref<scene_content> content;
  using prefab_dependency = content_dependency<prefab, ref<const prefab_description>>;
  std::map<std::string, std::shared_ptr<prefab_dependency>, std::less<>> required;
  explicit scene_loader(asset_identity identity, bool should_reload)
      : asset_loader(identity.uri), source(std::move(identity)), reload(should_reload) {}
  std::shared_ptr<const asset> reserve() override {
    if constexpr (std::is_same_v<T, scene>)
      return scene_asset_access::make<T>(source, ref<const scene_content>{});
    else
      return scene_asset_access::make<T>(source, ref<const D>{});
  }
  void decode(const std::vector<std::byte>& bytes) override {
    auto decoded = asset_file::deserialize_ref<D>(std::span<const std::byte>{bytes});
    status = decoded ? error::success : decoded.error().error;
    if (decoded) {
      if constexpr (std::is_same_v<T, scene>) {
        content = make_ref<scene_content>();
        content->description = std::move(*decoded);
        for (std::size_t index = 0; index < content->description->objects.size(); ++index) {
          const auto& object = content->description->objects[index];
          if (!object.prefab || required.contains(object.prefab->uri))
            continue;
          auto dependency = std::make_shared<prefab_dependency>(
              object.prefab->uri, reload, [](const prefab& value) { return value.snapshot(); });
          dependency->path = "/objects/" + std::to_string(index) + "/prefab";
          required.emplace(object.prefab->uri, dependency);
          dependencies.push_back(std::move(dependency));
        }
        value = scene_asset_access::make<T>(source, ref<const scene_content>{content});
      } else {
        value = scene_asset_access::make<T>(source, std::move(*decoded));
      }
    } else {
      diagnostic = std::move(decoded).error();
    }
    metrics.bytes = input_bytes;
  }
  error finish() override {
    if constexpr (std::is_same_v<T, scene>) {
      for (const auto& [uri, dependency] : required) {
        const auto& loaded = dependency->resource();
        content->prefabs.emplace(uri, scene_prefab_dependency{loaded, dependency->content(),
                                                              dependency->source_bytes()});
      }
      required.clear();
      content.reset();
    }
    return error::success;
  }
  void adopt(const std::shared_ptr<const void>& previous) override {
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
