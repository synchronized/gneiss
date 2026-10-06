// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/asset/png_decoder.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <new>
#include <string>
#include <vector>

namespace {
thread_local bool fail_allocations{};
thread_local std::size_t allocation_failures{};
}

// 仅本测试进程在选定调用期间持续拒绝标准分配，不改变解码器接口或第三方分配策略。
void* operator new(std::size_t size) {
  if (fail_allocations) {
    ++allocation_failures;
    throw std::bad_alloc{};
  }
  if (auto* memory = std::malloc(size == 0U ? 1U : size)) {
    return memory;
  }
  throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, [[maybe_unused]] std::size_t size) noexcept { std::free(memory); }

int main() {
  // 旧代码在诊断分配失败时会越过 noexcept；固定退出码防止弹出系统崩溃窗口。
  std::set_terminate([] { std::_Exit(99); });
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
  gneiss::asset_internal::decoded_png image;
  std::string message;
  if (gneiss::asset_internal::decode_png(bytes, image, message) != GNEISS_SUCCESS ||
      image.width != 1U || image.height != 1U || image.pixels.size() != 4U || !message.empty()) {
    return 1;
  }
  if (gneiss::asset_internal::decode_png(bytes, image, message, 3U) != GNEISS_ERROR_OUT_OF_MEMORY ||
      image.width != 0U || !image.pixels.empty() || message.empty()) {
    return 3;
  }
  if (gneiss::asset_internal::decode_png(bytes, image, message, 4U) != GNEISS_SUCCESS ||
      image.pixels.size() != 4U) {
    return 4;
  }
  gneiss::asset_internal::decoded_png allocation_image;
  std::string allocation_message;
  allocation_failures = 0U;
  fail_allocations = true;
  const auto allocation_result =
      gneiss::asset_internal::decode_png(bytes, allocation_image, allocation_message);
  fail_allocations = false;
  const bool pixel_failure_injected = allocation_failures != 0U;
  if (!pixel_failure_injected || allocation_result != GNEISS_ERROR_OUT_OF_MEMORY ||
      allocation_image.width != 0U || allocation_image.height != 0U ||
      !allocation_image.pixels.empty()) {
    return 5;
  }

  const std::vector<std::byte> empty_bytes;
  allocation_failures = 0U;
  fail_allocations = true;
  const auto empty_result =
      gneiss::asset_internal::decode_png(empty_bytes, allocation_image, allocation_message);
  // 部分标准库的短字符串存储可容纳整条诊断，此时没有分配可失败。
  fail_allocations = false;
  const bool message_failure_injected = allocation_failures != 0U;
  const auto expected =
      message_failure_injected ? GNEISS_ERROR_OUT_OF_MEMORY : GNEISS_ERROR_INVALID_ARGUMENT;
  if (empty_result != expected || allocation_image.width != 0U || allocation_image.height != 0U ||
      !allocation_image.pixels.empty()) {
    return 6;
  }
  if (gneiss::asset_internal::decode_png(empty_bytes, allocation_image, allocation_message) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      allocation_message.empty()) {
    return 7;
  }
  bytes.resize(16);
  if (gneiss::asset_internal::decode_png(bytes, image, message) != GNEISS_ERROR_INVALID_ARGUMENT ||
      image.width != 0U || image.height != 0U || !image.pixels.empty() || message.empty()) {
    return 2;
  }
  return 0;
}
