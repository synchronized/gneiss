// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_CORE_ENTITY_HPP_
#define GNEISS_CORE_ENTITY_HPP_

#include <gneiss/core/entity.h>

namespace gneiss {

/** 不拥有实体的强类型运行时标识。 */
class entity_id final {
public:
  constexpr entity_id() noexcept = default;
  explicit constexpr entity_id(gneiss_entity_id value) noexcept : value_(value) {}

  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != GNEISS_NULL_ENTITY_ID; }
  [[nodiscard]] constexpr gneiss_entity_id get() const noexcept { return value_; }

  friend constexpr bool operator==(entity_id, entity_id) noexcept = default;

private:
  gneiss_entity_id value_ = GNEISS_NULL_ENTITY_ID;
};

inline constexpr entity_id null_entity_id{};

} // namespace gneiss

#endif
