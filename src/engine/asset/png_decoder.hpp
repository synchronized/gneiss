// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_PNG_DECODER_HPP_
#define GNEISS_ASSET_PNG_DECODER_HPP_

#include <gneiss/engine/core/result.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gneiss::asset_internal {

struct decoded_png final {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<std::byte> pixels;
};

/** CPU 解码；失败时清空尺寸与有效像素，允许复用已有容量。
 * 诊断或像素分配失败返回 OUT_OF_MEMORY；并发调用不得共享可写输出。 */
[[nodiscard]] gneiss_result decode_png(const std::vector<std::byte>& bytes, decoded_png& out_image,
                                       std::string& out_message,
                                       std::size_t byte_limit = 256U * 1024U * 1024U) noexcept;

} // namespace gneiss::asset_internal

#endif
