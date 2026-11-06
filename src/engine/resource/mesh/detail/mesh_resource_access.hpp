// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/mesh.hpp"
#include "engine/resource/resource_retirement_queue.hpp"
#include <utility>
namespace gneiss::engine::detail {
/** 仅供资源装配与 Loader 使用；修改操作限所属线程。 */
struct mesh_resource_access final {
  template <typename Finalize>
  static std::shared_ptr<engine::mesh> make(resource_retirement_queue& queue, Finalize finalize,
                                            rid handle) {
    return queue.make_with_factory<engine::mesh>(std::move(finalize),
                                                 [handle] { return engine::mesh(handle); });
  }
  static rid handle(const engine::mesh& value) noexcept { return value.rid_; }
  static void set_source(const engine::mesh& value, asset_identity source) {
    value.identity_ = std::move(source);
  }

  static void set_metadata(const engine::mesh& value, mesh_metadata metadata) noexcept {
    value.metadata_ = std::move(metadata);
  }
};
} // namespace gneiss::engine::detail
