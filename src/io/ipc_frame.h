// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_IO_IPC_FRAME_H_
#define GNEISS_SRC_IO_IPC_FRAME_H_

#include <gneiss/core/result.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gneiss {

inline constexpr std::size_t ipc_frame_header_size = 16U;
inline constexpr std::size_t ipc_frame_max_payload_size = 1024U * 1024U;

struct ipc_frame final {
  std::uint16_t protocol_major = 0U;
  std::uint16_t protocol_minor = 0U;
  std::uint16_t message_type = 0U;
  std::uint16_t flags = 0U;
  std::vector<std::uint8_t> payload;
};

/** 将单个 IPC 帧编码为网络字节序；output 在成功时由本函数完整替换。 */
[[nodiscard]] result encode_ipc_frame(const ipc_frame& frame,
                                      std::vector<std::uint8_t>& output) noexcept;

/** 有界增量 IPC 帧解码器；解析失败后必须 reset() 才能继续使用。 */
class ipc_frame_decoder final {
public:
  [[nodiscard]] result append(std::span<const std::uint8_t> bytes,
                              std::vector<ipc_frame>& output) noexcept;
  void reset() noexcept;
  [[nodiscard]] bool has_failed() const noexcept;
  [[nodiscard]] std::size_t buffered_size() const noexcept;

private:
  std::vector<std::uint8_t> buffer_;
  bool has_failed_ = false;
};

} // namespace gneiss

#endif
