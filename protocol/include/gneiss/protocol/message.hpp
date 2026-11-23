// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/protocol/dispatcher.hpp>
#include <gneiss/protocol/payload.hpp>
namespace gneiss::protocol {
/** 消息协议事实；描述不拥有业务状态，validator 只检查数据合法性。 */
template <class T> struct message_descriptor {
  std::uint16_t domain;
  std::uint16_t operation;
  ipc::message_kind kind;
  peer_role sender;
  std::size_t max_payload_size;
  bool (*valid)(const T&);
  [[nodiscard]] constexpr operation_descriptor rule() const noexcept {
    return {operation, sender == peer_role::editor ? kind_mask(kind) : message_kind_mask{},
            sender == peer_role::runtime ? kind_mask(kind) : message_kind_mask{}};
  }
};
struct request_context {
  std::uint32_t id{};
};
/** 生成独立信封；关联 ID 遵循帧头规则。 */
template <class T>
[[nodiscard]] result<ipc::envelope, serialization::diagnostic>
encode(const message_descriptor<T>& message, const T& value, std::uint32_t request_id = 0) {
  ipc::envelope envelope{.domain = message.domain,
                         .operation = message.operation,
                         .kind = message.kind,
                         .request_id = request_id,
                         .payload = {}};
  if (ipc::validate_envelope(envelope).failed() || (message.valid && !message.valid(value)))
    return serialization::diagnostic{.message = "消息身份或数据无效",
                                     .error = error::invalid_argument};
  auto payload = encode_payload(value, message.max_payload_size);
  if (!payload)
    return std::move(payload).error();
  envelope.payload = std::move(*payload);
  return envelope;
}
/** 解码独立候选；握手、协商版本及发送方检查由路由负责。 */
template <class T>
[[nodiscard]] result<T, serialization::diagnostic> decode(const message_descriptor<T>& message,
                                                          const ipc::envelope& envelope) {
  if (ipc::validate_envelope(envelope).failed() || envelope.domain != message.domain ||
      envelope.operation != message.operation || envelope.kind != message.kind)
    return serialization::diagnostic{.message = "消息信封不匹配", .error = error::invalid_argument};
  auto value = decode_payload<T>(envelope.payload, message.max_payload_size);
  if (!value)
    return std::move(value).error();
  if (message.valid && !message.valid(*value))
    return serialization::diagnostic{.message = "消息数据无效", .error = error::invalid_argument};
  return value;
}
}
