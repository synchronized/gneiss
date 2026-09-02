// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_frame.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

gneiss::ipc_frame make_frame(std::uint16_t type, std::vector<std::uint8_t> payload) {
  gneiss::ipc_frame frame;
  frame.protocol_major = 1U;
  frame.protocol_minor = 2U;
  frame.message_type = type;
  frame.flags = 3U;
  frame.payload = std::move(payload);
  return frame;
}

bool is_expected(const gneiss::ipc_frame& frame, std::uint16_t type,
                 const std::vector<std::uint8_t>& payload) {
  return frame.protocol_major == 1U && frame.protocol_minor == 2U && frame.message_type == type &&
         frame.flags == 3U && frame.payload == payload;
}

bool test_split_at_every_byte() {
  const auto source = make_frame(7U, {1U, 2U, 3U, 4U, 5U});
  std::vector<std::uint8_t> encoded;
  if (gneiss::encode_ipc_frame(source, encoded) != gneiss::result::success) {
    return false;
  }
  const std::vector<std::uint8_t> expected_header{'G', 'N', 'I', 'P', 0U, 1U, 0U, 2U,
                                                  0U,  7U,  0U,  3U,  0U, 0U, 0U, 5U};
  if (!std::ranges::equal(expected_header,
                          std::span(encoded).first(gneiss::ipc_frame_header_size))) {
    return false;
  }
  for (std::size_t split = 0U; split <= encoded.size(); ++split) {
    gneiss::ipc_frame_decoder decoder;
    std::vector<gneiss::ipc_frame> frames;
    if (decoder.append(std::span(encoded).first(split), frames) != gneiss::result::success ||
        decoder.append(std::span(encoded).subspan(split), frames) != gneiss::result::success ||
        frames.size() != 1U || !is_expected(frames[0], 7U, source.payload) ||
        decoder.buffered_size() != 0U) {
      return false;
    }
  }
  return true;
}

bool test_sticky_frames() {
  const auto first = make_frame(8U, {6U, 7U});
  const auto second = make_frame(9U, {});
  std::vector<std::uint8_t> first_bytes;
  std::vector<std::uint8_t> second_bytes;
  if (gneiss::encode_ipc_frame(first, first_bytes) != gneiss::result::success ||
      gneiss::encode_ipc_frame(second, second_bytes) != gneiss::result::success) {
    return false;
  }
  first_bytes.insert(first_bytes.end(), second_bytes.begin(), second_bytes.end());
  gneiss::ipc_frame_decoder decoder;
  std::vector<gneiss::ipc_frame> frames;
  return decoder.append(first_bytes, frames) == gneiss::result::success && frames.size() == 2U &&
         is_expected(frames[0], 8U, first.payload) && is_expected(frames[1], 9U, second.payload);
}

bool test_byte_stream() {
  const auto source = make_frame(10U, {11U, 12U, 13U});
  std::vector<std::uint8_t> encoded;
  if (gneiss::encode_ipc_frame(source, encoded) != gneiss::result::success) {
    return false;
  }
  gneiss::ipc_frame_decoder decoder;
  std::vector<gneiss::ipc_frame> frames;
  for (const auto byte : encoded) {
    if (decoder.append(std::span(&byte, 1U), frames) != gneiss::result::success) {
      return false;
    }
  }
  return frames.size() == 1U && is_expected(frames[0], 10U, source.payload);
}

bool test_invalid_input_and_reset() {
  const auto source = make_frame(11U, {1U});
  std::vector<std::uint8_t> encoded;
  if (gneiss::encode_ipc_frame(source, encoded) != gneiss::result::success) {
    return false;
  }
  auto invalid_magic = encoded;
  invalid_magic[0] = 0U;
  gneiss::ipc_frame_decoder decoder;
  std::vector<gneiss::ipc_frame> frames;
  if (decoder.append(invalid_magic, frames) != gneiss::result::invalid_argument ||
      !decoder.has_failed() || decoder.append(encoded, frames) != gneiss::result::invalid_state) {
    return false;
  }
  decoder.reset();
  if (decoder.append(encoded, frames) != gneiss::result::success || frames.size() != 1U) {
    return false;
  }

  auto oversized = encoded;
  const auto length_offset = 12U;
  oversized[length_offset] = 0U;
  oversized[length_offset + 1U] = 0x10U;
  oversized[length_offset + 2U] = 0U;
  oversized[length_offset + 3U] = 1U;
  decoder.reset();
  frames.clear();
  return decoder.append(oversized, frames) == gneiss::result::invalid_argument &&
         decoder.has_failed();
}

bool test_payload_limit() {
  auto too_large =
      make_frame(12U, std::vector<std::uint8_t>(gneiss::ipc_frame_max_payload_size + 1U, 0U));
  std::vector<std::uint8_t> unchanged{42U};
  return gneiss::encode_ipc_frame(too_large, unchanged) == gneiss::result::invalid_argument &&
         unchanged == std::vector<std::uint8_t>{42U};
}

} // namespace

int main() {
  return test_split_at_every_byte() && test_sticky_frames() && test_byte_stream() &&
                 test_invalid_input_and_reset() && test_payload_limit()
             ? 0
             : 1;
}
