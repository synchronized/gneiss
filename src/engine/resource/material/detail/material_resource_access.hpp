// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/material/material_resource.hpp"
#include "engine/resource/resource_retirement_queue.hpp"
#include <utility>
namespace gneiss::engine::detail {
/** 仅供资源装配与 Loader 使用；修改操作限所属线程。 */
struct material_resource_access final {
  template <typename Finalize>
  static std::shared_ptr<material_resource> make(resource_retirement_queue& queue,
                                                 Finalize finalize, rid handle) {
    return queue.make_with_factory<material_resource>(
        std::move(finalize), [handle] { return material_resource(handle); });
  }
  static rid handle(const material_resource& value) noexcept { return value.rid_; }
  static void set_source(const material_resource& value, asset_identity source) {
    value.identity_ = std::move(source);
  }
  static std::uint64_t update_sequence(const material_resource& value) noexcept {
    return value.update_sequence_;
  }
  static bool claim_update(const material_resource& value, std::uint64_t sequence) noexcept {
    if (sequence == 0U || sequence < value.update_sequence_)
      return false;
    value.update_sequence_ = sequence;
    return true;
  }
  static void set_content(const material_resource& value, const material_parameters& parameters,
                          std::array<texture, 5> dependencies) noexcept {
    value.parameters_ = parameters;
    value.textures_ = std::move(dependencies);
  }
};
} // namespace gneiss::engine::detail
