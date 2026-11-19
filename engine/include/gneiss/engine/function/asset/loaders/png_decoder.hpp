// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_PNG_DECODER_HPP_
#define GNEISS_ASSET_PNG_DECODER_HPP_

#include <gneiss/engine/export.hpp>
#include <gneiss/engine/core/base/result.hpp>
#include <gneiss/engine/resource/asset_diagnostic.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gneiss {

struct decoded_png final {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<std::byte> pixels;
};

/** CPU 解码并返回独立像素数据；失败仅返回诊断。
 * 诊断或像素分配异常向上传播；显式字节预算拒绝仍返回 out_of_memory，可并发处理不同输入。 */
[[nodiscard]] GNEISS_ENGINE_API result<decoded_png, asset_diagnostic>
decode_png(const std::vector<std::byte>& bytes, std::size_t byte_limit = 256U * 1024U * 1024U);

} // namespace gneiss

#endif
