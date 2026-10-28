// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/mesh/mesh_resource.hpp"
#include "engine/resource/resource_retirement_queue.hpp"
#include <utility>
namespace gneiss::engine::detail {
/** 仅供资源装配与 Loader 使用；修改操作限所属线程。 */
struct mesh_resource_access final {
  template <typename Finalize>
  static std::shared_ptr<mesh_resource> make(resource_retirement_queue& queue, Finalize finalize,
                                             rid handle) {
    return queue.make_with_factory<mesh_resource>(std::move(finalize),
                                                  [handle] { return mesh_resource(handle); });
  }
  static rid handle(const mesh_resource& value) noexcept { return value.rid_; }
  static void set_source(const mesh_resource& value, asset_identity source) {
    value.identity_ = std::move(source);
  }
  static std::uint64_t update_sequence(const mesh_resource& value) noexcept {
    return value.update_sequence_;
  }
  static bool claim_update(const mesh_resource& value, std::uint64_t sequence) noexcept {
    if (sequence == 0U || sequence < value.update_sequence_)
      return false;
    value.update_sequence_ = sequence;
    return true;
  }
  static void set_metadata(const mesh_resource& value, mesh_metadata metadata) noexcept {
    value.metadata_ = std::move(metadata);
  }
};
} // namespace gneiss::engine::detail
