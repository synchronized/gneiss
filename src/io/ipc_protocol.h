// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_IO_IPC_PROTOCOL_H_
#define GNEISS_SRC_IO_IPC_PROTOCOL_H_

#include "ipc_frame.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss {

inline constexpr std::uint16_t ipc_protocol_major = 1U;
inline constexpr std::uint16_t ipc_protocol_minor = 1U;
inline constexpr std::size_t ipc_protocol_max_json_size = 64U * 1024U;
inline constexpr std::string_view ipc_capability_runtime_inspection_v1 = "runtime_inspection_v1";

enum class ipc_message_type : std::uint16_t {
  hello = 1U,
  hello_ack = 2U,
  ready = 3U,
  state_changed = 4U,
  pause = 5U,
  resume = 6U,
  stop = 7U,
  log_event = 8U,
  error = 9U,
  ping = 10U,
  pong = 11U,
  shutdown_complete = 12U,
};

enum class ipc_runtime_state : std::uint8_t {
  loading,
  ready,
  running,
  paused,
  stopping,
};

/** 内部协议的数据传输对象；仅与消息类型相关的字段有效。 */
struct ipc_message final {
  ipc_message_type type = ipc_message_type::error;
  std::string session_token;
  std::vector<std::string> capabilities;
  ipc_runtime_state runtime_state = ipc_runtime_state::loading;
  std::uint64_t nonce = 0U;
  std::int32_t code = 0;
  std::string text;
};

/** 单次 Runtime 会话内的对象标识；跨会话或 generation 不同时不得复用。 */
struct ipc_runtime_object_id final {
  std::uint64_t value = 0U;
  std::uint32_t generation = 0U;

  [[nodiscard]] bool is_valid() const noexcept { return value != 0U && generation != 0U; }
  [[nodiscard]] bool operator==(const ipc_runtime_object_id&) const noexcept = default;
};

/** 检查增量的会话与顺序标记。序号从 1 开始并在会话内严格递增。 */
struct ipc_inspection_stamp final {
  std::uint64_t session_id = 0U;
  std::uint64_t sequence = 0U;
};

enum class ipc_inspection_sequence_result : std::uint8_t {
  accepted,
  duplicate,
  gap,
  stale_session,
  invalid,
};

/** Editor 侧检查消息顺序跟踪器；发现缺口后由上层请求完整快照。 */
class ipc_inspection_sequence_tracker final {
public:
  [[nodiscard]] result begin(std::uint64_t session_id) noexcept;
  void reset() noexcept;
  [[nodiscard]] ipc_inspection_sequence_result observe(ipc_inspection_stamp stamp) noexcept;
  [[nodiscard]] std::uint64_t session_id() const noexcept { return session_id_; }
  [[nodiscard]] std::uint64_t next_sequence() const noexcept { return next_sequence_; }

private:
  std::uint64_t session_id_ = 0U;
  std::uint64_t next_sequence_ = 0U;
};

/** 编码有界 JSON 负载，并写入当前协议版本和消息类型。 */
[[nodiscard]] result encode_ipc_message(const ipc_message& message, ipc_frame& output) noexcept;

/** 解码并验证已知消息；未知消息返回 unsupported，同主版本的未来次版本可以读取。 */
[[nodiscard]] result decode_ipc_message(const ipc_frame& frame, ipc_message& output) noexcept;

/** 构造 Client 首个 hello。 */
[[nodiscard]] result make_ipc_hello(std::string_view session_token,
                                    std::span<const std::string> capabilities,
                                    ipc_frame& output) noexcept;

/** Server 校验 hello 并生成 hello_ack；能力交集保持 Client 请求顺序。 */
[[nodiscard]] result accept_ipc_hello(const ipc_frame& hello, std::string_view expected_token,
                                      std::span<const std::string> supported_capabilities,
                                      ipc_frame& acknowledgment,
                                      std::vector<std::string>& negotiated_capabilities) noexcept;

/** Client 校验 hello_ack，并返回 Server 接受的能力集合。 */
[[nodiscard]] result
accept_ipc_hello_ack(const ipc_frame& acknowledgment,
                     std::span<const std::string> requested_capabilities,
                     std::vector<std::string>& negotiated_capabilities) noexcept;

/** 使用调用方提供的单调时钟时间点判定握手或心跳超时。 */
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

} // namespace gneiss

#endif
