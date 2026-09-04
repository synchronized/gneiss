// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_SESSION_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_SESSION_PROTOCOL_H_

#include "ipc_dispatcher.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss {

inline constexpr std::uint16_t ipc_session_domain_version = 1U;
inline constexpr std::size_t ipc_session_max_payload_size = std::size_t{64U} * 1024U;

enum class ipc_session_operation : std::uint16_t {
  hello = 1U,
  heartbeat = 2U,
  protocol_error = 3U,
  shutdown_complete = 4U,
};

/** 使用调用方提供的单调时钟时间点判定会话超时。 */
class ipc_timeout_tracker final {
public:
  using clock = std::chrono::steady_clock;

  explicit ipc_timeout_tracker(clock::duration timeout) noexcept;
  void reset(clock::time_point now) noexcept;
  [[nodiscard]] bool expired(clock::time_point now) const noexcept;

private:
  clock::duration timeout_;
  clock::time_point last_observed_{};
};

struct ipc_session_hello final {
  std::string token;
  std::vector<ipc_domain_capability> domains;
};

struct ipc_session_heartbeat final {
  std::uint64_t nonce = 0U;
};

struct ipc_session_error final {
  std::int32_t code = 0;
  std::string message;
};

struct ipc_session_shutdown final {
  std::int32_t exit_code = 0;
};

[[nodiscard]] result encode_ipc_session_hello(const ipc_session_hello& message, bool response,
                                              std::uint32_t request_id,
                                              ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_session_hello(const ipc_envelope& envelope,
                                              ipc_session_hello& output) noexcept;

[[nodiscard]] result encode_ipc_session_heartbeat(const ipc_session_heartbeat& message,
                                                  bool response, std::uint32_t request_id,
                                                  ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_session_heartbeat(const ipc_envelope& envelope,
                                                  ipc_session_heartbeat& output) noexcept;

[[nodiscard]] result encode_ipc_session_error(const ipc_session_error& message,
                                              std::uint32_t request_id,
                                              ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_session_error(const ipc_envelope& envelope,
                                              ipc_session_error& output) noexcept;

[[nodiscard]] result encode_ipc_session_shutdown(const ipc_session_shutdown& message,
                                                 ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_session_shutdown(const ipc_envelope& envelope,
                                                 ipc_session_shutdown& output) noexcept;

/** 校验 Hello 令牌并按请求顺序协商双方均支持的域版本。 */
[[nodiscard]] result
negotiate_ipc_session_hello(const ipc_session_hello& request, std::string_view expected_token,
                            std::span<const ipc_domain_capability> supported_domains,
                            ipc_session_hello& acknowledgment) noexcept;

/** 返回 Session 域的固定方向与消息语义规则。 */
[[nodiscard]] std::span<const ipc_operation_descriptor> ipc_session_operations() noexcept;

} // namespace gneiss

#endif
