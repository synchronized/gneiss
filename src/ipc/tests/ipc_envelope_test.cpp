// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_envelope.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

[[nodiscard]] gneiss::ipc_envelope make_request() {
  return {.domain = gneiss::ipc_domain::property,
          .operation = 7U,
          .kind = gneiss::ipc_message_kind::request,
          .request_id = 42U,
          .payload = {1U, 2U, 3U}};
}

[[nodiscard]] bool test_round_trip_and_network_order() {
  const auto request = make_request();
  std::vector<std::uint8_t> encoded;
  if (gneiss::encode_ipc_envelope(request, encoded) != gneiss::result::success ||
      encoded.size() != gneiss::ipc_envelope_header_size + request.payload.size()) {
    return false;
  }
  constexpr std::array expected_header{
      std::uint8_t{0x47}, std::uint8_t{0x4E}, std::uint8_t{0x49}, std::uint8_t{0x32},
      std::uint8_t{0x00}, std::uint8_t{0x02}, std::uint8_t{0x00}, std::uint8_t{0x00},
      std::uint8_t{0x00}, std::uint8_t{0x06}, std::uint8_t{0x00}, std::uint8_t{0x07},
      std::uint8_t{0x00}, std::uint8_t{0x02}, std::uint8_t{0x00}, std::uint8_t{0x00},
      std::uint8_t{0x00}, std::uint8_t{0x00}, std::uint8_t{0x00}, std::uint8_t{0x2A},
      std::uint8_t{0x00}, std::uint8_t{0x00}, std::uint8_t{0x00}, std::uint8_t{0x03}};
  if (!std::ranges::equal(expected_header,
                          std::span{encoded}.first(gneiss::ipc_envelope_header_size))) {
    return false;
  }

  gneiss::ipc_envelope_decoder decoder;
  std::vector<gneiss::ipc_envelope> decoded;
  const auto split = gneiss::ipc_envelope_header_size - 1U;
  return decoder.append(std::span{encoded}.first(split), decoded) == gneiss::result::success &&
         decoded.empty() && decoder.buffered_size() == split &&
         decoder.append(std::span{encoded}.subspan(split), decoded) == gneiss::result::success &&
         decoded.size() == 1U && decoded[0].domain == request.domain &&
         decoded[0].operation == request.operation && decoded[0].kind == request.kind &&
         decoded[0].request_id == request.request_id && decoded[0].payload == request.payload;
}

[[nodiscard]] bool test_sticky_failure_and_reset() {
  std::vector<std::uint8_t> encoded;
  if (gneiss::encode_ipc_envelope(make_request(), encoded) != gneiss::result::success) {
    return false;
  }
  encoded[14] = 1U;
  gneiss::ipc_envelope_decoder decoder;
  std::vector<gneiss::ipc_envelope> output;
  if (decoder.append(encoded, output) != gneiss::result::invalid_argument ||
      !decoder.has_failed() || decoder.append(encoded, output) != gneiss::result::invalid_state) {
    return false;
  }
  decoder.reset();
  encoded[14] = 0U;
  return !decoder.has_failed() && decoder.append(encoded, output) == gneiss::result::success &&
         output.size() == 1U;
}

[[nodiscard]] bool test_validation() {
  auto envelope = make_request();
  envelope.request_id = 0U;
  if (gneiss::validate_ipc_envelope(envelope) != gneiss::result::invalid_argument) {
    return false;
  }
  envelope.kind = gneiss::ipc_message_kind::event;
  envelope.request_id = 1U;
  if (gneiss::validate_ipc_envelope(envelope) != gneiss::result::invalid_argument) {
    return false;
  }
  envelope.request_id = 0U;
  envelope.operation = 0U;
  if (gneiss::validate_ipc_envelope(envelope) != gneiss::result::invalid_argument) {
    return false;
  }
  envelope.operation = 1U;
  envelope.protocol_major = 3U;
  if (gneiss::validate_ipc_envelope(envelope) != gneiss::result::invalid_argument) {
    return false;
  }
  envelope.protocol_major = gneiss::ipc_v2_protocol_major;
  envelope.protocol_minor = 1U;
  if (gneiss::validate_ipc_envelope(envelope) != gneiss::result::invalid_argument) {
    return false;
  }
  envelope.protocol_minor = gneiss::ipc_v2_protocol_minor;
  envelope.domain = static_cast<gneiss::ipc_domain>(0U);
  if (gneiss::validate_ipc_envelope(envelope) != gneiss::result::invalid_argument) {
    return false;
  }
  envelope.domain = static_cast<gneiss::ipc_domain>(99U);
  if (gneiss::validate_ipc_envelope(envelope) != gneiss::result::success) {
    return false;
  }
  envelope.kind = static_cast<gneiss::ipc_message_kind>(99U);
  return gneiss::validate_ipc_envelope(envelope) == gneiss::result::invalid_argument;
}

[[nodiscard]] bool test_malformed_headers() {
  std::vector<std::uint8_t> encoded;
  if (gneiss::encode_ipc_envelope(make_request(), encoded) != gneiss::result::success) {
    return false;
  }

  for (const auto mutation : {0U, 12U, 20U}) {
    auto malformed = encoded;
    if (mutation == 0U) {
      malformed[0] = 0U;
    } else if (mutation == 12U) {
      malformed[12] = 0U;
      malformed[13] = 99U;
    } else {
      malformed[20] = 0U;
      malformed[21] = 0x10U;
      malformed[22] = 0U;
      malformed[23] = 1U;
    }
    gneiss::ipc_envelope_decoder decoder;
    std::vector<gneiss::ipc_envelope> output;
    if (decoder.append(malformed, output) != gneiss::result::invalid_argument || !output.empty()) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool test_concatenated_envelopes() {
  auto first = make_request();
  auto second = first;
  second.kind = gneiss::ipc_message_kind::response;
  std::vector<std::uint8_t> first_bytes;
  std::vector<std::uint8_t> second_bytes;
  if (gneiss::encode_ipc_envelope(first, first_bytes) != gneiss::result::success ||
      gneiss::encode_ipc_envelope(second, second_bytes) != gneiss::result::success) {
    return false;
  }
  first_bytes.insert(first_bytes.end(), second_bytes.begin(), second_bytes.end());
  gneiss::ipc_envelope_decoder decoder;
  std::vector<gneiss::ipc_envelope> output;
  return decoder.append(first_bytes, output) == gneiss::result::success && output.size() == 2U &&
         output[0].kind == gneiss::ipc_message_kind::request &&
         output[1].kind == gneiss::ipc_message_kind::response;
}

} // namespace

int main() {
  try {
    return test_round_trip_and_network_order() && test_sticky_failure_and_reset() &&
                   test_validation() && test_malformed_headers() && test_concatenated_envelopes()
               ? 0
               : 1;
  } catch (...) {
    return 1;
  }
}
