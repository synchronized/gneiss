// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_frame.h"

#include <algorithm>
#include <limits>
#include <new>

namespace {

constexpr std::uint32_t frame_magic = UINT32_C(0x474E4950);
constexpr std::size_t max_decoder_buffer_size =
    (gneiss::ipc_frame_header_size + gneiss::ipc_frame_max_payload_size) * 2U;

void write_u16(std::uint8_t* destination, std::uint16_t value) noexcept {
  destination[0] = static_cast<std::uint8_t>(value >> 8U);
  destination[1] = static_cast<std::uint8_t>(value);
}

void write_u32(std::uint8_t* destination, std::uint32_t value) noexcept {
  destination[0] = static_cast<std::uint8_t>(value >> 24U);
  destination[1] = static_cast<std::uint8_t>(value >> 16U);
  destination[2] = static_cast<std::uint8_t>(value >> 8U);
  destination[3] = static_cast<std::uint8_t>(value);
}

std::uint16_t read_u16(const std::uint8_t* source) noexcept {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(source[0]) << 8U) |
                                    static_cast<std::uint16_t>(source[1]));
}

std::uint32_t read_u32(const std::uint8_t* source) noexcept {
  return (static_cast<std::uint32_t>(source[0]) << 24U) |
         (static_cast<std::uint32_t>(source[1]) << 16U) |
         (static_cast<std::uint32_t>(source[2]) << 8U) | static_cast<std::uint32_t>(source[3]);
}

} // namespace

namespace gneiss {

result encode_ipc_frame(const ipc_frame& frame, std::vector<std::uint8_t>& output) noexcept {
  if (frame.payload.size() > ipc_frame_max_payload_size ||
      frame.payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    return result::invalid_argument;
  }
  try {
    std::vector<std::uint8_t> encoded(ipc_frame_header_size + frame.payload.size());
    write_u32(encoded.data(), frame_magic);
    write_u16(encoded.data() + 4U, frame.protocol_major);
    write_u16(encoded.data() + 6U, frame.protocol_minor);
    write_u16(encoded.data() + 8U, frame.message_type);
    write_u16(encoded.data() + 10U, frame.flags);
    write_u32(encoded.data() + 12U, static_cast<std::uint32_t>(frame.payload.size()));
    std::ranges::copy(frame.payload, encoded.begin() + ipc_frame_header_size);
    output = std::move(encoded);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result ipc_frame_decoder::append(std::span<const std::uint8_t> bytes,
                                 std::vector<ipc_frame>& output) noexcept {
  if (has_failed_) {
    return result::invalid_state;
  }
  if (bytes.empty()) {
    return result::success;
  }
  if (bytes.size() > max_decoder_buffer_size ||
      buffer_.size() > max_decoder_buffer_size - bytes.size()) {
    has_failed_ = true;
    buffer_.clear();
    return result::invalid_argument;
  }

  try {
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    std::size_t offset = 0U;
    while (buffer_.size() - offset >= ipc_frame_header_size) {
      const auto* header = buffer_.data() + offset;
      if (read_u32(header) != frame_magic) {
        has_failed_ = true;
        buffer_.clear();
        return result::invalid_argument;
      }
      const auto payload_size = static_cast<std::size_t>(read_u32(header + 12U));
      if (payload_size > ipc_frame_max_payload_size) {
        has_failed_ = true;
        buffer_.clear();
        return result::invalid_argument;
      }
      const auto frame_size = ipc_frame_header_size + payload_size;
      if (buffer_.size() - offset < frame_size) {
        break;
      }

      ipc_frame decoded;
      decoded.protocol_major = read_u16(header + 4U);
      decoded.protocol_minor = read_u16(header + 6U);
      decoded.message_type = read_u16(header + 8U);
      decoded.flags = read_u16(header + 10U);
      decoded.payload.assign(header + ipc_frame_header_size, header + frame_size);
      output.push_back(std::move(decoded));
      offset += frame_size;
    }
    if (offset != 0U) {
      buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    return result::success;
  } catch (const std::bad_alloc&) {
    has_failed_ = true;
    buffer_.clear();
    return result::out_of_memory;
  } catch (...) {
    has_failed_ = true;
    buffer_.clear();
    return result::internal;
  }
}

void ipc_frame_decoder::reset() noexcept {
  buffer_.clear();
  has_failed_ = false;
}

bool ipc_frame_decoder::has_failed() const noexcept { return has_failed_; }

std::size_t ipc_frame_decoder::buffered_size() const noexcept { return buffer_.size(); }

} // namespace gneiss
