// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include <unordered_set>
namespace gneiss::engine::serialization {
class reader;
namespace detail {
struct context_access;
}
/** 不可复制的调用上下文；配置只读，运行状态私有。可顺序复用，不可并发共用。 */
class context final {
  friend struct detail::context_access;
  meta::registry_ref registry_;
  bool use_default_ = true;
  std::size_t maximum_depth_ = 64;
  mutable std::unordered_set<const void*> active_{};
  mutable reader* input_{};
  mutable std::size_t depth_{};
  mutable std::size_t operations_{};

public:
  context() = default;
  /** 显式空注册表仍无效，不回退到默认表。 */
  explicit context(meta::registry_ref registry, std::size_t maximum_depth = 64) noexcept
      : registry_(std::move(registry)), use_default_(false), maximum_depth_(maximum_depth) {}
  explicit context(std::size_t maximum_depth) noexcept : maximum_depth_(maximum_depth) {}
  context(const context&) = delete;
  context& operator=(const context&) = delete;
  [[nodiscard]] GNEISS_API result<meta::registry_ref> registry() const noexcept;
  [[nodiscard]] std::size_t maximum_depth() const noexcept { return maximum_depth_; }
};
}
