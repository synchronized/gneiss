// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/asset/detail/loader_factory.hpp"
#include "engine/resource/scene/detail/scene_asset_access.hpp"
namespace gneiss::engine::detail {
namespace {
template <class T, class D> struct scene_loader final : asset_loader {
  asset_identity source;
  explicit scene_loader(asset_identity identity)
      : asset_loader(identity.uri), source(std::move(identity)) {}
  void decode(const std::vector<std::byte>& bytes) override {
    auto decoded = asset_file::deserialize_ref<D>(std::span<const std::byte>{bytes});
    status = decoded ? error::success : decoded.error().error;
    if (decoded)
      value = scene_asset_access::make<T>(source, std::move(*decoded));
    else
      diagnostic = std::move(decoded).error();
    metrics.bytes = input_bytes;
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
std::shared_ptr<asset_loader> make_scene_loader(asset_identity source, bool is_prefab) {
  if (is_prefab)
    return std::make_shared<scene_loader<prefab, prefab_description>>(std::move(source));
  return std::make_shared<scene_loader<scene, scene_description>>(std::move(source));
}
}
