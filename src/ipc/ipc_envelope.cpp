// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_envelope.h"

#include <algorithm>
#include <limits>
#include <new>

namespace {

constexpr std::uint32_t envelope_magic = UINT32_C(0x474E4932);
constexpr std::size_t max_decoder_buffer_size =
    (gneiss::ipc_envelope_header_size + gneiss::ipc_envelope_max_payload_size) * 2U;

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

[[nodiscard]] std::uint16_t read_u16(const std::uint8_t* source) noexcept {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(source[0]) << 8U) |
                                    static_cast<std::uint16_t>(source[1]));
}

[[nodiscard]] std::uint32_t read_u32(const std::uint8_t* source) noexcept {
  return (static_cast<std::uint32_t>(source[0]) << 24U) |
         (static_cast<std::uint32_t>(source[1]) << 16U) |
         (static_cast<std::uint32_t>(source[2]) << 8U) | static_cast<std::uint32_t>(source[3]);
}

[[nodiscard]] bool is_known_kind(gneiss::ipc_message_kind kind) noexcept {
  switch (kind) {
  case gneiss::ipc_message_kind::event:
  case gneiss::ipc_message_kind::request:
  case gneiss::ipc_message_kind::response:
  case gneiss::ipc_message_kind::error:
    return true;
  }
  return false;
}

} // namespace

namespace gneiss {

result validate_ipc_envelope(const ipc_envelope& envelope) noexcept {
  if (envelope.protocol_major != ipc_v2_protocol_major ||
      envelope.protocol_minor > ipc_v2_protocol_minor ||
      static_cast<std::uint16_t>(envelope.domain) == 0U || envelope.operation == 0U ||
      !is_known_kind(envelope.kind) || envelope.payload.size() > ipc_envelope_max_payload_size ||
      envelope.payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    return result::invalid_argument;
  }
  if ((envelope.kind == ipc_message_kind::event && envelope.request_id != 0U) ||
      (envelope.kind != ipc_message_kind::event && envelope.request_id == 0U)) {
    return result::invalid_argument;
  }
  return result::success;
}

result encode_ipc_envelope(const ipc_envelope& envelope,
                           std::vector<std::uint8_t>& output) noexcept {
  if (validate_ipc_envelope(envelope) != result::success) {
    return result::invalid_argument;
  }
  try {
    std::vector<std::uint8_t> encoded(ipc_envelope_header_size + envelope.payload.size());
    write_u32(encoded.data(), envelope_magic);
    write_u16(encoded.data() + 4U, envelope.protocol_major);
    write_u16(encoded.data() + 6U, envelope.protocol_minor);
    write_u16(encoded.data() + 8U, static_cast<std::uint16_t>(envelope.domain));
    write_u16(encoded.data() + 10U, envelope.operation);
    write_u16(encoded.data() + 12U, static_cast<std::uint16_t>(envelope.kind));
    write_u16(encoded.data() + 14U, 0U);
    write_u32(encoded.data() + 16U, envelope.request_id);
    write_u32(encoded.data() + 20U, static_cast<std::uint32_t>(envelope.payload.size()));
    std::ranges::copy(envelope.payload, encoded.begin() + ipc_envelope_header_size);
    output = std::move(encoded);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result ipc_envelope_decoder::append(std::span<const std::uint8_t> bytes,
                                    std::vector<ipc_envelope>& output) noexcept {
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
    while (buffer_.size() - offset >= ipc_envelope_header_size) {
      const auto* header = buffer_.data() + offset;
      if (read_u32(header) != envelope_magic || read_u16(header + 14U) != 0U) {
        has_failed_ = true;
        buffer_.clear();
        return result::invalid_argument;
      }
      const auto payload_size = static_cast<std::size_t>(read_u32(header + 20U));
      if (payload_size > ipc_envelope_max_payload_size) {
        has_failed_ = true;
        buffer_.clear();
        return result::invalid_argument;
      }
      const auto envelope_size = ipc_envelope_header_size + payload_size;
      if (buffer_.size() - offset < envelope_size) {
        break;
      }

      ipc_envelope decoded;
      decoded.protocol_major = read_u16(header + 4U);
      decoded.protocol_minor = read_u16(header + 6U);
      decoded.domain = static_cast<ipc_domain>(read_u16(header + 8U));
      decoded.operation = read_u16(header + 10U);
      decoded.kind = static_cast<ipc_message_kind>(read_u16(header + 12U));
      decoded.request_id = read_u32(header + 16U);
      decoded.payload.assign(header + ipc_envelope_header_size, header + envelope_size);
      if (validate_ipc_envelope(decoded) != result::success) {
        has_failed_ = true;
        buffer_.clear();
        return result::invalid_argument;
      }
      output.push_back(std::move(decoded));
      offset += envelope_size;
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

void ipc_envelope_decoder::reset() noexcept {
  buffer_.clear();
  has_failed_ = false;
}

bool ipc_envelope_decoder::has_failed() const noexcept { return has_failed_; }

std::size_t ipc_envelope_decoder::buffered_size() const noexcept { return buffer_.size(); }

} // namespace gneiss
