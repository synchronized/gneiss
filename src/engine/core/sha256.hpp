// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gneiss::core {

using sha256_digest = std::array<std::byte, 32U>;

/** 无堆分配的增量摘要；digest 不修改状态，可继续追加。输入总长度按 SHA-256 的 64 位位长编码。 */
class sha256_builder final {
public:
  void update(std::span<const std::byte> bytes) noexcept;
  [[nodiscard]] sha256_digest digest() const noexcept;

private:
  void process(std::span<const std::byte> chunk) noexcept;
  std::array<std::uint32_t, 8U> state_ = {
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
  };
  std::array<std::byte, 64U> pending_{};
  std::size_t pending_size_{};
  std::uint64_t byte_count_{};
};

/** 计算内存数据的 SHA-256；无堆分配，用于内容版本校验与离线资产摘要。 */
[[nodiscard]] sha256_digest sha256(std::span<const std::byte> bytes) noexcept;

} // namespace gneiss::core
