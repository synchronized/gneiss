// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/resource/asset.hpp"
#include "engine/resource/prefab.hpp"
#include "engine/resource/scene/scene_description.hpp"
#include <map>
namespace gneiss::engine {
namespace detail {
struct scene_asset_access;
}
/** 场景结构依赖同时保留资产身份和本次加载的固定内容。 */
struct scene_prefab_dependency {
  ref<const prefab> resource;
  ref<const prefab_description> description;
  std::size_t source_bytes{};
};
struct scene_content {
  ref<const scene_description> description;
  std::map<std::string, scene_prefab_dependency, std::less<>> prefabs;
};
/** 共享资产身份；实例化使用独立内容快照，重载不修改已有 World 实例。 */
class scene final : public asset {
public:
  /** 所属线程取得不可变快照；快照可交给工作线程并独立于后续重载存活。 */
  [[nodiscard]] ref<const scene_description> snapshot() const noexcept {
    return content_ ? content_->description : ref<const scene_description>{};
  }
  /** 描述与依赖作为同一不可变快照取得；独立重载 Prefab 不改变此快照。 */
  [[nodiscard]] ref<const scene_content> content() const noexcept { return content_; }

private:
  friend struct detail::scene_asset_access;
  scene(asset_identity source, ref<const scene_content> content)
      : asset(runtime_type_of<scene>()), content_(std::move(content)) {
    identity_ = std::move(source);
  }
  mutable ref<const scene_content> content_;
};
}
