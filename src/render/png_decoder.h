// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_PNG_DECODER_H_
#define GNEISS_RENDER_PNG_DECODER_H_

#include <gneiss/core/result.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gneiss::render_internal {

struct decoded_png final {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<std::byte> pixels;
};

[[nodiscard]] gneiss_result decode_png(const std::vector<std::byte>& bytes, decoded_png& out_image,
                                       std::string& out_message) noexcept;

} // namespace gneiss::render_internal

#endif
