// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gneiss::ipc {

inline constexpr std::uint16_t v2_protocol_major = 2U;
inline constexpr std::uint16_t v2_protocol_minor = 0U;
inline constexpr std::size_t envelope_header_size = 24U;
inline constexpr std::size_t envelope_max_payload_size = 1024U * 1024U;

enum class message_kind : std::uint16_t {
  event = 1U,
  request = 2U,
  response = 3U,
  error = 4U,
};

/** IPC v2 通用信封；域内负载由对应协议解析。 */
struct envelope final {
  std::uint16_t protocol_major = v2_protocol_major;
  std::uint16_t protocol_minor = v2_protocol_minor;
  std::uint16_t domain = 0U;
  std::uint16_t operation = 0U;
  message_kind kind = message_kind::event;
  std::uint32_t request_id = 0U;
  std::vector<std::uint8_t> payload;
};

/** 校验信封结构与请求关联约束，不解析域内负载。 */
[[nodiscard]] error validate_envelope(const envelope& envelope) noexcept;

/** 将信封编码为固定网络字节序；成功时完整替换 output。 */
[[nodiscard]] error encode_envelope(const envelope& envelope, std::vector<std::uint8_t>& output);

/** 有界增量 IPC v2 信封解码器；结构失败后必须 reset() 才能继续使用。 */
class envelope_decoder final {
public:
  [[nodiscard]] error append(std::span<const std::uint8_t> bytes, std::vector<envelope>& output);
  void reset() noexcept;
  [[nodiscard]] bool has_failed() const noexcept;
  [[nodiscard]] std::size_t buffered_size() const noexcept;

private:
  std::vector<std::uint8_t> buffer_;
  bool has_failed_ = false;
};

} // namespace gneiss::ipc
