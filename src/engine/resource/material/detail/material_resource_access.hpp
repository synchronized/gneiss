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
    auto token = queue.make_with_factory<rid>(std::move(finalize), [handle] { return handle; });
    auto value = std::shared_ptr<engine::material>(new engine::material(handle));
    value->retirement_token_ = std::move(token);
    return value;
  }
  static rid handle(const engine::material& value) noexcept { return value.rid_; }
  static void set_source(const engine::material& value, asset_identity source) {
    value.identity_ = std::move(source);
  }

  static void set_content(const engine::material& value,
                          ref<const material_content> content) noexcept {
    value.content_ = std::move(content);
  }
};
} // namespace gneiss::engine::detail
