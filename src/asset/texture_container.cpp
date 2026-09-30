// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/texture_container.h"

#include <algorithm>
#include <array>
#include <new>

namespace gneiss::asset_internal {

gneiss_result texture_container::open(std::unique_ptr<read_source> source,
                                      std::size_t manifest_limit,
                                      std::string& diagnostic) noexcept {
  source_.reset();
  layout_ = {};
  std::vector<std::byte>{}.swap(manifest_);
  diagnostic.clear();
  if (!source) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    std::array<std::byte, 64> header{};
    if (source->size() < header.size()) {
      diagnostic = "运行纹理 Header 截断";
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    auto result = source->read_at(0U, header);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    texture_binary_layout layout;
    const auto decoded = decode_texture_binary_header(header, source->size(), layout, diagnostic);
    if (decoded != texture_binary_result::success) {
      if (decoded == texture_binary_result::out_of_memory) {
        return GNEISS_ERROR_OUT_OF_MEMORY;
      }
      return decoded == texture_binary_result::unsupported_version ? GNEISS_ERROR_UNSUPPORTED
                                                                   : GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (layout.manifest_size > manifest_limit) {
      diagnostic = "运行纹理 Manifest 超出读取预算";
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
    std::vector<std::byte> manifest(static_cast<std::size_t>(layout.manifest_size));
    result = source->read_at(layout.manifest_offset, manifest);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    const auto padding_offset = layout.manifest_offset + layout.manifest_size;
    std::array<std::byte, 15> padding{};
    const auto used =
        std::span{padding}.first(static_cast<std::size_t>(layout.payload_offset - padding_offset));
    if (!used.empty()) {
      result = source->read_at(padding_offset, used);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      if (!std::ranges::all_of(used, [](std::byte value) { return value == std::byte{}; })) {
        diagnostic = "运行纹理对齐填充非零";
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
    }
    layout_ = layout;
    manifest_ = std::move(manifest);
    source_ = std::move(source);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result texture_container::read_payload(std::uint64_t offset,
                                              std::span<std::byte> output) const noexcept {
  if (!source_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  if (offset > layout_.payload_size || output.size() > layout_.payload_size - offset) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return source_->read_at(layout_.payload_offset + offset, output);
}

gneiss_result texture_payload_source::read(std::vector<std::byte>& output,
                                           std::size_t limit) const noexcept {
  output.clear();
  if (!container_ || offset_ > container_->payload_size() ||
      size_ > container_->payload_size() - offset_) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  if (size_ > limit) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
  try {
    std::vector<std::byte> bytes(static_cast<std::size_t>(size_));
    auto result = container_->read_payload(offset_, bytes);
    if (result == GNEISS_SUCCESS && core::sha256(bytes) != digest_) {
      result = GNEISS_ERROR_INVALID_STATE;
    }
    if (result == GNEISS_SUCCESS) {
      output = std::move(bytes);
    }
    return result;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::asset_internal
