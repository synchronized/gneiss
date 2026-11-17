// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_CORE_IPC_ROUTER_H_
#define GNEISS_APPS_COMMON_IPC_CORE_IPC_ROUTER_H_

#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/ipc_protocol_domains.hpp>

#include <algorithm>
#include <functional>
#include <gneiss/protocol/message.hpp>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace gneiss::host {

template <typename Message> struct ipc_route_result final {
  gneiss::protocol::dispatch_outcome outcome;
  std::optional<Message> message;

  [[nodiscard]] bool accepted() const noexcept { return outcome.accepted() && message.has_value(); }
};

/** 将通用协议校验、域路由和强类型消息输出组合为单一入口。 */
template <typename Message> class ipc_router final {
public:
  using decoder = gneiss::error (*)(const gneiss::ipc::envelope&, Message&);

  ipc_router() {
    initialized_ = register_ipc_v2_domains(accept, this, dispatcher_) == gneiss::error::success;
  }

  ipc_router(const ipc_router&) = delete;
  ipc_router& operator=(const ipc_router&) = delete;

  [[nodiscard]] gneiss::error bind(std::uint16_t domain, decoder decode) {
    if (!initialized_ || decode == nullptr ||
        std::ranges::find(bindings_, domain, &binding::domain) != bindings_.end()) {
      return gneiss::error::invalid_argument;
    }
    {
      bindings_.push_back({domain, decode});
      return gneiss::error::success;
    }
  }

  [[nodiscard]] ipc_route_result<Message>
  dispatch(const gneiss::ipc::envelope& envelope,
           const gneiss::protocol::dispatch_context& context) {
    ipc_route_result<Message> routed;
    if (!initialized_) {
      routed.outcome = {.rejection = gneiss::protocol::dispatch_rejection::handler_failed,
                        .handler_result = gneiss::error::invalid_state};
      return routed;
    }
    pending_.reset();
    routed.outcome = dispatcher_.dispatch(envelope, context);
    if (routed.outcome.accepted()) {
      routed.message = std::move(pending_);
      if (!routed.message) {
        routed.outcome = {.rejection = gneiss::protocol::dispatch_rejection::handler_failed,
                          .handler_result = gneiss::error::internal};
      }
    }
    pending_.reset();
    return routed;
  }

private:
  struct binding final {
    std::uint16_t domain = gneiss::protocol::domain::session;
    decoder decode = nullptr;
  };

  static gneiss::error accept(void* context, const gneiss::ipc::envelope& envelope) {
    auto& self = *static_cast<ipc_router*>(context);
    const auto match = std::ranges::find(self.bindings_, envelope.domain, &binding::domain);
    if (match == self.bindings_.end()) {
      return gneiss::error::unsupported;
    }
    Message message;
    const auto operation = match->decode(envelope, message);
    if (operation == gneiss::error::success) {
      self.pending_ = std::move(message);
    }
    return operation;
  }

  gneiss::protocol::dispatcher dispatcher_;
  bool initialized_{};
  std::vector<binding> bindings_;
  std::optional<Message> pending_;
};

/**
 * 按“协议域 + 操作”将信封交给强类型上下文处理器。
 *
 * Router 不拥有调用上下文，上下文仅在当前调用期间借用；绑定修改与分发由所属线程串行执行。
 */
template <typename Context> class ipc_operation_router final {
public:
  using handler = gneiss::error (*)(const gneiss::ipc::envelope&, Context&);

  ipc_operation_router() {
    initialized_ = register_ipc_v2_domains(accept, this, dispatcher_) == gneiss::error::success;
  }

  ipc_operation_router(const ipc_operation_router&) = delete;
  ipc_operation_router& operator=(const ipc_operation_router&) = delete;

  [[nodiscard]] gneiss::error bind(std::uint16_t domain, std::uint16_t operation, handler handle) {
    const auto duplicate = std::ranges::find_if(bindings_, [&](const binding& value) {
      return value.domain == domain && value.operation == operation;
    });
    if (!initialized_ || operation == 0U || handle == nullptr || duplicate != bindings_.end()) {
      return gneiss::error::invalid_argument;
    }
    {
      bindings_.push_back({domain, operation, handle});
      return gneiss::error::success;
    }
  }

  template <class T, class Handler>
  [[nodiscard]] error bind(const protocol::message_descriptor<T>& message, Handler handle) {
    const auto duplicate = std::ranges::find_if(bindings_, [&](const binding& value) {
      return value.domain == message.domain && value.operation == message.operation &&
             (!value.kind || *value.kind == message.kind);
    });
    if (!initialized_ || duplicate != bindings_.end())
      return error::invalid_argument;
    bindings_.push_back(
        {message.domain, message.operation,
         [message, handle](const ipc::envelope& envelope, Context& context) -> error {
           auto value = protocol::decode(message, envelope);
           if (!value)
             return value.error().error;
           return handle(context, protocol::request_context{envelope.request_id},
                         std::move(*value));
         },
         message.kind, message.sender});
    return error::success;
  }

  [[nodiscard]] protocol::dispatch_outcome
  dispatch(const ipc::envelope& envelope, const protocol::dispatch_context& dispatch_context,
           Context& context) {
    if (!initialized_)
      return {.rejection = protocol::dispatch_rejection::handler_failed,
              .handler_result = error::invalid_state};
    auto outcome = dispatcher_.validate(envelope, dispatch_context);
    if (!outcome.accepted())
      return outcome;
    const auto match = std::ranges::find_if(bindings_, [&](const binding& value) {
      return value.domain == envelope.domain && value.operation == envelope.operation &&
             (!value.kind || *value.kind == envelope.kind);
    });
    if (match != bindings_.end() && match->sender && *match->sender != dispatch_context.remote_role)
      return {.rejection = protocol::dispatch_rejection::wrong_direction};
    const auto status =
        match == bindings_.end() ? error::unsupported : match->handle(envelope, context);
    return status.ok() ? protocol::dispatch_outcome{}
                       : protocol::dispatch_outcome{
                             .rejection = protocol::dispatch_rejection::handler_failed,
                             .handler_result = status};
  }

private:
  struct binding final {
    std::uint16_t domain = gneiss::protocol::domain::session;
    std::uint16_t operation = 0U;
    std::function<error(const ipc::envelope&, Context&)> handle;
    std::optional<ipc::message_kind> kind{};
    std::optional<protocol::peer_role> sender{};
  };

  static error accept(void*, const ipc::envelope&) { return error::unsupported; }

  gneiss::protocol::dispatcher dispatcher_;
  bool initialized_{};
  std::vector<binding> bindings_;
};

} // namespace gneiss::host

#endif
