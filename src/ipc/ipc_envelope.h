// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_IPC_IPC_ENVELOPE_H_
#define GNEISS_SRC_IPC_IPC_ENVELOPE_H_

#include <gneiss/core/result.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gneiss {

inline constexpr std::uint16_t ipc_v2_protocol_major = 2U;
inline constexpr std::uint16_t ipc_v2_protocol_minor = 0U;
inline constexpr std::size_t ipc_envelope_header_size = 24U;
inline constexpr std::size_t ipc_envelope_max_payload_size = 1024U * 1024U;

enum class ipc_domain : std::uint16_t {
  session = 1U,
  control = 2U,
  log = 3U,
  inspection = 4U,
  statistics = 5U,
  property = 6U,
};

enum class ipc_message_kind : std::uint16_t {
  event = 1U,
  request = 2U,
  response = 3U,
  error = 4U,
};

/** IPC v2 通用信封；域内负载由对应协议解析。 */
struct ipc_envelope final {
  std::uint16_t protocol_major = ipc_v2_protocol_major;
  std::uint16_t protocol_minor = ipc_v2_protocol_minor;
  ipc_domain domain = ipc_domain::session;
  std::uint16_t operation = 0U;
  ipc_message_kind kind = ipc_message_kind::event;
  std::uint32_t request_id = 0U;
  std::vector<std::uint8_t> payload;
};

/** 校验信封结构与请求关联约束，不解析域内负载。 */
[[nodiscard]] result validate_ipc_envelope(const ipc_envelope& envelope) noexcept;

/** 将信封编码为固定网络字节序；成功时完整替换 output。 */
[[nodiscard]] result encode_ipc_envelope(const ipc_envelope& envelope,
                                         std::vector<std::uint8_t>& output) noexcept;

/** 有界增量 IPC v2 信封解码器；结构失败后必须 reset() 才能继续使用。 */
class ipc_envelope_decoder final {
public:
  [[nodiscard]] result append(std::span<const std::uint8_t> bytes,
                              std::vector<ipc_envelope>& output) noexcept;
  void reset() noexcept;
  [[nodiscard]] bool has_failed() const noexcept;
  [[nodiscard]] std::size_t buffered_size() const noexcept;

private:
  std::vector<std::uint8_t> buffer_;
  bool has_failed_ = false;
};

} // namespace gneiss

#endif
