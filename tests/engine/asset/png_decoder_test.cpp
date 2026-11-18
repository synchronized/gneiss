// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/asset/loaders/png_decoder.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <new>
#include <string>
#include <vector>

namespace {
enum class allocation_target { none, pixel_buffer, diagnostic_text };
thread_local allocation_target fail_allocations{};
thread_local std::size_t allocation_failures{};
}

// 针对 1×1 RGBA 像素与超出 SSO 的诊断文本持续拒绝分配。
// MSVC Debug 的字符串／vector 代理可在 noexcept 构造或移动中分配，
// 只注入这两个分配点并验证异常传播，保留代理分配与迭代器检查。
void* operator new(std::size_t size) {
  if ((fail_allocations == allocation_target::pixel_buffer && size == 4U) ||
      (fail_allocations == allocation_target::diagnostic_text && size >= 32U)) {
    ++allocation_failures;
    throw std::bad_alloc{};
  }
  if (auto* memory = std::malloc(size == 0U ? 1U : size)) {
    return memory;
  }
  throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, [[maybe_unused]] std::size_t size) noexcept {
  std::free(memory);
}

int main() {
  gneiss::install_fatal_handler();
  constexpr std::array<std::uint8_t, 68> png{
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
      0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x04, 0x00, 0x00,
      0x00, 0xB5, 0x1C, 0x0C, 0x02, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54, 0x78,
      0xDA, 0x63, 0x64, 0xF8, 0x0F, 0x00, 0x01, 0x05, 0x01, 0x01, 0x27, 0x18, 0xE3, 0x66,
      0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
  std::vector<std::byte> bytes;
  bytes.reserve(png.size());
  for (const auto value : png) {
    bytes.push_back(static_cast<std::byte>(value));
  }
  using namespace gneiss;
  auto image = decode_png(bytes);
  if (!image || image->width != 1U || image->height != 1U || image->pixels.size() != 4U)
    return 1;
  auto limited = decode_png(bytes, 3U);
  if (limited || limited.error().error != error::out_of_memory || limited.error().message.empty())
    return 3;
  auto exact = decode_png(bytes, 4U);
  if (!exact || exact->pixels.size() != 4U)
    return 4;
  allocation_failures = 0U;
  fail_allocations = allocation_target::pixel_buffer;
  bool pixel_threw = false;
  try {
    (void)decode_png(bytes);
  } catch (const std::bad_alloc&) {
    pixel_threw = true;
  }
  fail_allocations = allocation_target::none;
  if (!allocation_failures || !pixel_threw)
    return 5;
  const std::vector<std::byte> empty_bytes;
  allocation_failures = 0U;
  fail_allocations = allocation_target::diagnostic_text;
  bool diagnostic_threw = false;
  try {
    (void)decode_png(empty_bytes);
  } catch (const std::bad_alloc&) {
    diagnostic_threw = true;
  }
  fail_allocations = allocation_target::none;
  if (bool(allocation_failures) != diagnostic_threw)
    return 6;
  auto empty = decode_png(empty_bytes);
  if (empty || empty.error().error != error::invalid_argument || empty.error().message.empty())
    return 7;
  bytes.resize(16);
  auto truncated = decode_png(bytes);
  if (truncated || truncated.error().error != error::invalid_argument ||
      truncated.error().message.empty())
    return 2;
  return 0;
}
