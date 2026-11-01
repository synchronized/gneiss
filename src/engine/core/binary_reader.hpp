// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gneiss::engine {
/** 借用只读字节；源存储必须覆盖 reader 及返回 span 的生命周期。
 * 越界返回 invalid_argument，失败不移动游标；不分配内存、不提供线程同步。 */
class binary_reader final {
public:
  explicit binary_reader(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}
  [[nodiscard]] std::size_t position() const noexcept { return position_; }
  [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - position_; }
  [[nodiscard]] result<std::span<const std::byte>> read_bytes(std::size_t count) noexcept {
    if (count > remaining())
      return error::invalid_argument;
    auto value = bytes_.subspan(position_, count);
    position_ += count;
    return value;
  }
  /** 按绝对偏移读取，不改变游标；减法检查避免 offset + sizeof(T) 溢出。 */
  template <std::unsigned_integral T>
    requires(!std::same_as<T, bool> && sizeof(T) <= sizeof(std::uint64_t))
  [[nodiscard]] result<T> read_at_le(std::size_t offset) const noexcept {
    if (offset > bytes_.size() || sizeof(T) > bytes_.size() - offset)
      return error::invalid_argument;
    std::uint64_t value{};
    for (std::size_t index = 0; index < sizeof(T); ++index)
      value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes_[offset + index]))
               << (index * 8U);
    return static_cast<T>(value);
  }
  template <std::unsigned_integral T>
    requires(!std::same_as<T, bool> && sizeof(T) <= sizeof(std::uint64_t))
  [[nodiscard]] result<T> read_le() noexcept {
    auto value = read_at_le<T>(position_);
    if (value)
      position_ += sizeof(T);
    return value;
  }
  [[nodiscard]] result<std::uint16_t> read_u16_le() noexcept { return read_le<std::uint16_t>(); }
  [[nodiscard]] result<std::uint32_t> read_u32_le() noexcept { return read_le<std::uint32_t>(); }
  [[nodiscard]] result<std::uint64_t> read_u64_le() noexcept { return read_le<std::uint64_t>(); }
  [[nodiscard]] result<float> read_f32_le() noexcept {
    auto value = read_u32_le();
    if (!value)
      return value.error();
    return std::bit_cast<float>(*value);
  }
  [[nodiscard]] result<float> read_f32_at_le(std::size_t offset) const noexcept {
    auto value = read_at_le<std::uint32_t>(offset);
    if (!value)
      return value.error();
    return std::bit_cast<float>(*value);
  }

private:
  std::span<const std::byte> bytes_;
  std::size_t position_{};
};
} // namespace gneiss::engine
