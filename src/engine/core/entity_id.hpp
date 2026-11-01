// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_CORE_ENTITY_HPP_
#define GNEISS_CORE_ENTITY_HPP_

#include <cstdint>

namespace gneiss {

/** 不拥有实体的强类型运行时标识。 */
class entity_id final {
public:
  constexpr entity_id() noexcept = default;
  explicit constexpr entity_id(std::uint64_t value) noexcept : value_(value) {}

  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != 0; }
  [[nodiscard]] constexpr std::uint64_t get() const noexcept { return value_; }

  friend constexpr bool operator==(entity_id, entity_id) noexcept = default;

private:
  std::uint64_t value_ = 0;
};

inline constexpr entity_id null_entity_id{};

} // namespace gneiss

#endif
