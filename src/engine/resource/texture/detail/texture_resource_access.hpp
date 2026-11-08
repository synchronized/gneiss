// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/resource_retirement_queue.hpp"
#include "engine/resource/texture.hpp"
#include <utility>
namespace gneiss::engine::detail {
/** 仅供资源装配与 Loader 使用；修改操作限所属线程。 */
struct texture_resource_access final {
  template <typename Finalize>
  static std::shared_ptr<engine::texture> make(resource_retirement_queue& queue, Finalize finalize,
                                               rid handle) {
    auto token = queue.make_with_factory<rid>(std::move(finalize), [handle] { return handle; });
    auto value = std::shared_ptr<engine::texture>(new engine::texture(handle));
    value->retirement_token_ = std::move(token);
    return value;
  }
  static void set_content(const engine::texture& value, ref<const texture_data> content) noexcept {
    value.content_ = std::move(content);
  }
  static rid handle(const engine::texture& value) noexcept { return value.rid_; }
  static void set_source(const engine::texture& value, asset_identity source) {
    value.identity_ = std::move(source);
  }

  static void set_metadata(const engine::texture& value, texture_metadata metadata) noexcept {
    value.metadata_ = std::move(metadata);
  }
};
} // namespace gneiss::engine::detail
