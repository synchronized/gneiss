// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/material.hpp"
#include "engine/resource/resource_retirement_queue.hpp"
#include <utility>
namespace gneiss::engine::detail {
/** 仅供资源装配与 Loader 使用；修改操作限所属线程。 */
struct material_resource_access final {
  template <typename Finalize>
  static std::shared_ptr<engine::material> make(resource_retirement_queue& queue, Finalize finalize,
                                                rid handle) {
    return queue.make_with_factory<engine::material>(std::move(finalize),
                                                     [handle] { return engine::material(handle); });
  }
  static rid handle(const engine::material& value) noexcept { return value.rid_; }
  static void set_source(const engine::material& value, asset_identity source) {
    value.identity_ = std::move(source);
  }

  static void set_content(const engine::material& value, const material_parameters& parameters,
                          std::array<ref<const engine::texture>, 5> dependencies) noexcept {
    value.parameters_ = parameters;
    value.textures_ = std::move(dependencies);
  }
};
} // namespace gneiss::engine::detail
