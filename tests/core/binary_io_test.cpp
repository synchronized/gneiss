// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/binary_reader.hpp"
#include "engine/core/binary_writer.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <limits>

int main() {
  using namespace gneiss::engine;
  std::vector<std::byte> bytes;
  binary_writer writer{bytes};
  writer.write_u16_le(0x1234U);
  writer.write_u32_le(0x89abcdefU);
  writer.write_u64_le(UINT64_C(0x0123456789abcdef));
  writer.write_f32_le(-0.0F);
  writer.write_zeros(2);
  const std::array tail{std::byte{0xaa}, std::byte{0xbb}};
  writer.write_bytes(tail);
  const std::array<std::byte, 22> expected{
      std::byte{0x34}, std::byte{0x12}, std::byte{0xef}, std::byte{0xcd}, std::byte{0xab},
      std::byte{0x89}, std::byte{0xef}, std::byte{0xcd}, std::byte{0xab}, std::byte{0x89},
      std::byte{0x67}, std::byte{0x45}, std::byte{0x23}, std::byte{0x01}, std::byte{0},
      std::byte{0},    std::byte{0},    std::byte{0x80}, std::byte{0},    std::byte{0},
      std::byte{0xaa}, std::byte{0xbb}};
  if (writer.position() != expected.size() || !std::ranges::equal(bytes, expected))
    return 1;
  binary_reader reader{bytes};
  auto u16 = reader.read_u16_le();
  auto u32 = reader.read_u32_le();
  auto u64 = reader.read_u64_le();
  auto f32 = reader.read_f32_le();
  if (!u16 || *u16 != 0x1234U || !u32 || *u32 != 0x89abcdefU || !u64 ||
      *u64 != UINT64_C(0x0123456789abcdef) || !f32 ||
      std::bit_cast<std::uint32_t>(*f32) != 0x80000000U)
    return 2;
  auto rest = reader.read_bytes(4);
  if (!rest || rest->data() != bytes.data() + 18 || reader.remaining() != 0)
    return 3;
  auto eof = reader.read_u16_le();
  auto huge = reader.read_bytes(std::numeric_limits<std::size_t>::max());
  auto overflow = reader.read_at_le<std::uint64_t>(std::numeric_limits<std::size_t>::max());
  if (eof || eof.error() != error::invalid_argument || huge || overflow ||
      reader.position() != bytes.size())
    return 4;
  auto empty = reader.read_bytes(0);
  if (!empty || !empty->empty())
    return 5;
  auto at = reader.read_at_le<std::uint32_t>(2);
  auto at_float = reader.read_f32_at_le(14);
  if (!at || *at != 0x89abcdefU || !at_float ||
      std::bit_cast<std::uint32_t>(*at_float) != 0x80000000U || reader.position() != bytes.size())
    return 6;
  // 每种不足长度必须拒绝读取，失败后仍能领取原有字节。
  for (std::size_t length = 0; length < sizeof(std::uint64_t); ++length) {
    binary_reader truncated{std::span<const std::byte>{bytes}.first(length)};
    if (truncated.read_u64_le() || truncated.position() != 0)
      return 7;
    auto original = truncated.read_bytes(length);
    if (!original || original->size() != length)
      return 8;
  }
  binary_reader short_float{std::span<const std::byte>{bytes}.first(3)};
  if (short_float.read_f32_le() || short_float.position() != 0)
    return 9;
  binary_reader no_bytes{{}};
  if (no_bytes.read_u32_le() || no_bytes.remaining() != 0)
    return 10;
  return 0;
}
