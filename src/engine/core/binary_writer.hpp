// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gneiss::engine {
/** 借用字节容器并追加编码；容器必须覆盖 writer 生命周期。
 * 分配异常交由调用方处理；不保证一组写入操作的原子性，不提供线程同步。 */
class binary_writer final {
public:
  explicit binary_writer(std::vector<std::byte>& output) noexcept : output_(output) {}
  [[nodiscard]] std::size_t position() const noexcept { return output_.size(); }
  /** 输入不得引用输出容器的存储，避免扩容使输入失效。 */
  void write_bytes(std::span<const std::byte> bytes) {
    output_.insert(output_.end(), bytes.begin(), bytes.end());
  }
  void write_zeros(std::size_t count) { output_.insert(output_.end(), count, std::byte{}); }
  template <std::unsigned_integral T>
    requires(!std::same_as<T, bool> && sizeof(T) <= sizeof(std::uint64_t))
  void write_le(T value) {
    const auto bits = static_cast<std::uint64_t>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index)
      output_.push_back(static_cast<std::byte>((bits >> (index * 8U)) & 0xffU));
  }
  void write_u16_le(std::uint16_t value) { write_le(value); }
  void write_u32_le(std::uint32_t value) { write_le(value); }
  void write_u64_le(std::uint64_t value) { write_le(value); }
  void write_f32_le(float value) { write_le(std::bit_cast<std::uint32_t>(value)); }

private:
  std::vector<std::byte>& output_;
};
} // namespace gneiss::engine
