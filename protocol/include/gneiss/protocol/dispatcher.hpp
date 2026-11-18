// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/core/base/guid.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/ipc/envelope.hpp>
#include <gneiss/protocol/domains.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::protocol {

enum class peer_role : std::uint8_t {
  editor,
  runtime,
};

using message_kind_mask = std::uint16_t;

[[nodiscard]] constexpr message_kind_mask kind_mask(ipc::message_kind kind) noexcept {
  const auto value = static_cast<std::uint16_t>(kind);
  return value == 0U || value > 15U ? 0U : static_cast<message_kind_mask>(1U << (value - 1U));
}

struct operation_descriptor final {
  std::uint16_t operation = 0U;
  message_kind_mask editor_to_runtime_kinds = 0U;
  message_kind_mask runtime_to_editor_kinds = 0U;
};

using domain_handler = gneiss::error (*)(void* context, const ipc::envelope& envelope);

struct domain_descriptor final {
  std::uint16_t domain = domain::session;
  std::uint16_t version = 0U;
  std::string_view capability;
  std::size_t max_payload_size = 0U;
  std::span<const operation_descriptor> operations;
  domain_handler handler = nullptr;
  void* handler_context = nullptr;
};

struct domain_capability final {
  static constexpr auto type_guid = guid::literal("f87e7e72-ad48-443f-bef7-ce29758c207f");
  static constexpr std::string_view type_name = "gneiss.protocol.domain_capability";
  std::uint16_t domain = domain::session;
  std::uint16_t version = 0U;

  [[nodiscard]] bool operator==(const domain_capability&) const noexcept = default;
};

struct dispatch_context final {
  peer_role remote_role = peer_role::runtime;
  bool handshake_complete = false;
  std::span<const domain_capability> negotiated_domains;
};

enum class dispatch_rejection : std::uint8_t {
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

struct dispatch_outcome final {
  dispatch_rejection rejection = dispatch_rejection::none;
  gneiss::error handler_result = gneiss::error::success;

  [[nodiscard]] bool accepted() const noexcept { return rejection == dispatch_rejection::none; }
};

/** 拥有域描述和操作规则副本；处理器上下文的生命周期由组合层保证。 */
class dispatcher final {
public:
  [[nodiscard]] gneiss::error register_domain(const domain_descriptor& descriptor);
  [[nodiscard]] dispatch_outcome dispatch(const ipc::envelope&, const dispatch_context&) const;
  /** 仅校验协议规则，不调用处理器，供类型化路由使用。 */
  [[nodiscard]] dispatch_outcome validate(const ipc::envelope&, const dispatch_context&) const;
  void clear() noexcept;
  [[nodiscard]] std::size_t size() const noexcept;

private:
  struct registered_domain final {
    std::uint16_t domain = domain::session;
    std::uint16_t version = 0U;
    std::string capability;
    std::size_t max_payload_size = 0U;
    std::vector<operation_descriptor> operations;
    domain_handler handler = nullptr;
    void* handler_context = nullptr;
  };

  [[nodiscard]] const registered_domain* find(std::uint16_t domain) const noexcept;

  std::vector<registered_domain> domains_;
};

} // namespace gneiss::protocol
