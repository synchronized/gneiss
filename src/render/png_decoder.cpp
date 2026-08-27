// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/png_decoder.h"

#include <spng.h>

#include <limits>
#include <memory>
#include <new>

namespace gneiss::render_internal {
namespace {

constexpr std::uint32_t maximum_dimension = 16384U;
constexpr std::size_t maximum_decoded_bytes = 256U * 1024U * 1024U;

using context_ptr = std::unique_ptr<spng_ctx, decltype(&spng_ctx_free)>;

gneiss_result fail(int error, std::string& message) noexcept {
  try {
    const char* text = spng_strerror(error);
    message = text != nullptr ? text : "PNG 解码失败";
  } catch (...) {
    message.clear();
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
  return error == SPNG_EMEM ? GNEISS_ERROR_OUT_OF_MEMORY : GNEISS_ERROR_INVALID_ARGUMENT;
}

} // namespace

gneiss_result decode_png(const std::vector<std::byte>& bytes, decoded_png& out_image,
                         std::string& out_message) noexcept {
  out_image = {};
  out_message.clear();
  if (bytes.empty()) {
    out_message = "PNG 数据为空";
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    context_ptr context(spng_ctx_new(0), &spng_ctx_free);
    if (!context) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
    int result = spng_set_image_limits(context.get(), maximum_dimension, maximum_dimension);
    if (result == 0) {
      result = spng_set_png_buffer(context.get(), bytes.data(), bytes.size());
    }
    spng_ihdr header{};
    if (result == 0) {
      result = spng_get_ihdr(context.get(), &header);
    }
    std::size_t decoded_size = 0;
    if (result == 0) {
      result = spng_decoded_image_size(context.get(), SPNG_FMT_RGBA8, &decoded_size);
    }
    if (result != 0) {
      return fail(result, out_message);
    }
    if (decoded_size == 0U || decoded_size > maximum_decoded_bytes ||
        decoded_size != static_cast<std::size_t>(header.width) * header.height * 4U) {
      out_message = "PNG 解码尺寸超出限制";
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    out_image.width = header.width;
    out_image.height = header.height;
    out_image.pixels.resize(decoded_size);
    result = spng_decode_image(context.get(), out_image.pixels.data(), out_image.pixels.size(),
                               SPNG_FMT_RGBA8, SPNG_DECODE_TRNS);
    if (result != 0) {
      out_image = {};
      return fail(result, out_message);
    }
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    out_image = {};
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    out_image = {};
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::render_internal
