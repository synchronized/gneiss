// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_IO_IPC_DISPATCHER_H_
#define GNEISS_SRC_IO_IPC_DISPATCHER_H_

#include "ipc_envelope.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss {

enum class ipc_peer_role : std::uint8_t {
  editor,
  runtime,
};

enum class ipc_message_direction : std::uint8_t {
  editor_to_runtime = 1U,
  runtime_to_editor = 2U,
  bidirectional = 3U,
};

using ipc_message_kind_mask = std::uint16_t;

[[nodiscard]] constexpr ipc_message_kind_mask ipc_kind_mask(ipc_message_kind kind) noexcept {
  const auto value = static_cast<std::uint16_t>(kind);
  return value == 0U || value > 15U ? 0U : static_cast<ipc_message_kind_mask>(1U << (value - 1U));
}

struct ipc_operation_descriptor final {
  std::uint16_t operation = 0U;
  ipc_message_kind_mask allowed_kinds = 0U;
  ipc_message_direction direction = ipc_message_direction::bidirectional;
};

using ipc_domain_handler = result (*)(void* context, const ipc_envelope& envelope) noexcept;

struct ipc_domain_descriptor final {
  ipc_domain domain = ipc_domain::session;
  std::uint16_t version = 0U;
  std::string_view capability;
  std::size_t max_payload_size = 0U;
  std::span<const ipc_operation_descriptor> operations;
  ipc_domain_handler handler = nullptr;
  void* handler_context = nullptr;
};

struct ipc_domain_capability final {
  ipc_domain domain = ipc_domain::session;
  std::uint16_t version = 0U;
};

struct ipc_dispatch_context final {
  ipc_peer_role remote_role = ipc_peer_role::runtime;
  bool handshake_complete = false;
  std::span<const ipc_domain_capability> negotiated_domains;
};

enum class ipc_dispatch_rejection : std::uint8_t {
  none,
  invalid_envelope,
  handshake_required,
  domain_not_negotiated,
  unknown_domain,
  unsupported_domain_version,
  unknown_operation,
  wrong_direction,
  wrong_kind,
  payload_too_large,
  handler_failed,
};

struct ipc_dispatch_outcome final {
  ipc_dispatch_rejection rejection = ipc_dispatch_rejection::none;
  result handler_result = result::success;

  [[nodiscard]] bool accepted() const noexcept { return rejection == ipc_dispatch_rejection::none; }
};

/** 拥有域描述和操作规则副本；处理器上下文的生命周期由组合层保证。 */
class ipc_domain_registry final {
public:
  [[nodiscard]] result register_domain(const ipc_domain_descriptor& descriptor) noexcept;
  void clear() noexcept;
  [[nodiscard]] std::size_t size() const noexcept;

private:
  struct registered_domain final {
    ipc_domain domain = ipc_domain::session;
    std::uint16_t version = 0U;
    std::string capability;
    std::size_t max_payload_size = 0U;
    std::vector<ipc_operation_descriptor> operations;
    ipc_domain_handler handler = nullptr;
    void* handler_context = nullptr;
  };

  [[nodiscard]] const registered_domain* find(ipc_domain domain) const noexcept;

  std::vector<registered_domain> domains_;

  friend class ipc_dispatcher;
};

/** 只读使用注册表并执行通用协议校验；调用方须保证注册表和处理器上下文有效。 */
class ipc_dispatcher final {
public:
  explicit ipc_dispatcher(const ipc_domain_registry& registry) noexcept;

  [[nodiscard]] ipc_dispatch_outcome dispatch(const ipc_envelope& envelope,
                                              const ipc_dispatch_context& context) const noexcept;

private:
  const ipc_domain_registry* registry_;
};

} // namespace gneiss

#endif
