// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/resource_retirement_queue.hpp"
#include "engine/resource/texture/texture_resource.hpp"
#include <utility>
namespace gneiss::engine::detail {
/** 仅供资源装配与 Loader 使用；修改操作限所属线程。 */
struct texture_resource_access final {
  template <typename Finalize>
  static std::shared_ptr<texture_resource> make(resource_retirement_queue& queue, Finalize finalize,
                                                rid handle) {
    return queue.make_with_factory<texture_resource>(std::move(finalize),
                                                     [handle] { return texture_resource(handle); });
  }
  static rid handle(const texture_resource& value) noexcept { return value.rid_; }
  static void set_source(const texture_resource& value, asset_identity source) {
    value.identity_ = std::move(source);
  }
  static std::uint64_t update_sequence(const texture_resource& value) noexcept {
    return value.update_sequence_;
  }
  static bool claim_update(const texture_resource& value, std::uint64_t sequence) noexcept {
    if (sequence == 0U || sequence < value.update_sequence_)
      return false;
    value.update_sequence_ = sequence;
    return true;
  }
  static void set_metadata(const texture_resource& value, texture_metadata metadata) noexcept {
    value.metadata_ = std::move(metadata);
  }
};
} // namespace gneiss::engine::detail
