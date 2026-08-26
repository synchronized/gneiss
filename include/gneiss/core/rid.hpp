// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_CORE_RID_HPP_
#define GNEISS_CORE_RID_HPP_

#include <gneiss/core/rid.h>

namespace gneiss {

/** 不拥有资源的强类型 RID 包装。 */
class rid final {
public:
  constexpr rid() noexcept = default;
  explicit constexpr rid(gneiss_rid value) noexcept : value_(value) {}

  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != GNEISS_NULL_RID; }
  [[nodiscard]] constexpr gneiss_rid get() const noexcept { return value_; }

  friend constexpr bool operator==(rid, rid) noexcept = default;

private:
  gneiss_rid value_ = GNEISS_NULL_RID;
};

inline constexpr rid null_rid{};

} // namespace gneiss

#endif
