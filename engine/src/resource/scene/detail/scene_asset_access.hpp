// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/resource/prefab.hpp>
#include <gneiss/engine/resource/scene.hpp>
namespace gneiss::detail {
/** Loader 的构造与所属线程内容采用入口，不暴露可变描述。 */
struct scene_asset_access {
  template <class T, class D>
  static std::shared_ptr<const T> make(asset_identity source, ref<const D> content) {
    return std::shared_ptr<const T>(new T(std::move(source), std::move(content)));
  }
  template <class T> static void adopt(const T& target, const T& candidate) {
    target.content_ = candidate.content_;
  }
};
}
