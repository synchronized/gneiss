// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/sha256.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace gneiss::core {
namespace {

constexpr std::array<std::uint32_t, 64U> round_constants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U,
};

[[nodiscard]] std::uint32_t read_big_endian(std::span<const std::byte> bytes,
                                            std::size_t offset) noexcept {
  return std::to_integer<std::uint32_t>(bytes[offset]) << 24U |
         std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 16U |
         std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 8U |
         std::to_integer<std::uint32_t>(bytes[offset + 3U]);
}

} // namespace

void sha256_builder::process(std::span<const std::byte> chunk) noexcept {
  std::array<std::uint32_t, 64U> words{};
  for (std::size_t index = 0U; index < 16U; ++index) {
    words[index] = read_big_endian(chunk, index * 4U);
  }
  for (std::size_t index = 16U; index < words.size(); ++index) {
    const auto s0 = std::rotr(words[index - 15U], 7) ^ std::rotr(words[index - 15U], 18) ^
                    (words[index - 15U] >> 3U);
    const auto s1 = std::rotr(words[index - 2U], 17) ^ std::rotr(words[index - 2U], 19) ^
                    (words[index - 2U] >> 10U);
    words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
  }
  auto [a, b, c, d, e, f, g, h] = state_;
  for (std::size_t index = 0U; index < words.size(); ++index) {
    const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
    const auto choice = (e & f) ^ (~e & g);
    const auto temporary1 = h + sum1 + choice + round_constants[index] + words[index];
    const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
    const auto majority = (a & b) ^ (a & c) ^ (b & c);
    h = g;
    g = f;
    f = e;
    e = d + temporary1;
    d = c;
    c = b;
    b = a;
    a = temporary1 + sum0 + majority;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void sha256_builder::update(std::span<const std::byte> bytes) noexcept {
  byte_count_ += static_cast<std::uint64_t>(bytes.size());
  if (pending_size_ != 0U) {
    const auto count = std::min(bytes.size(), pending_.size() - pending_size_);
    std::ranges::copy(bytes.first(count),
                      pending_.begin() + static_cast<std::ptrdiff_t>(pending_size_));
    pending_size_ += count;
    bytes = bytes.subspan(count);
    if (pending_size_ == pending_.size()) {
      process(pending_);
      pending_size_ = 0U;
    }
  }
  while (bytes.size() >= 64U) {
    process(bytes.first(64U));
    bytes = bytes.subspan(64U);
  }
  if (!bytes.empty()) {
    std::ranges::copy(bytes, pending_.begin());
    pending_size_ = bytes.size();
  }
}

sha256_digest sha256_builder::digest() const noexcept {
  auto finalized = *this;
  const auto bit_length = byte_count_ * 8U;
  std::array<std::byte, 128U> tail{};
  std::ranges::copy(std::span{pending_}.first(pending_size_), tail.begin());
  tail[pending_size_] = std::byte{0x80};
  const auto padded = pending_size_ < 56U ? 64U : 128U;
  for (std::size_t index = 0; index < 8U; ++index) {
    tail[padded - 1U - index] = static_cast<std::byte>(bit_length >> (index * 8U));
  }
  finalized.process(std::span(tail).first(64U));
  if (padded == 128U) {
    finalized.process(std::span(tail).subspan(64U));
  }
  sha256_digest digest{};
  for (std::size_t word = 0U; word < state_.size(); ++word) {
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
      digest[(word * 4U) + byte] =
          static_cast<std::byte>(finalized.state_[word] >> ((3U - byte) * 8U));
    }
  }
  return digest;
}

sha256_digest sha256(std::span<const std::byte> bytes) noexcept {
  sha256_builder builder;
  builder.update(bytes);
  return builder.digest();
}

} // namespace gneiss::core
