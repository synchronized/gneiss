// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_ROUTER_H_
#define GNEISS_APPS_COMMON_IPC_ROUTER_H_

#include "ipc_protocol_domains.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace gneiss {

template <typename Message> struct ipc_route_result final {
  ipc_dispatch_outcome outcome;
  std::optional<Message> message;

  [[nodiscard]] bool accepted() const noexcept { return outcome.accepted() && message.has_value(); }
};

/** 将通用协议校验、域路由和强类型消息输出组合为单一入口。 */
template <typename Message> class ipc_router final {
public:
  using decoder = result (*)(const ipc_envelope&, Message&) noexcept;

  ipc_router() noexcept {
    const ipc_protocol_domain_handlers handlers{.session = accept,
                                                .control = accept,
                                                .log = accept,
                                                .inspection = accept,
                                                .statistics = accept,
                                                .property = accept,
                                                .context = this};
    if (register_ipc_v2_domains(handlers, registry_) == result::success) {
      dispatcher_ = std::make_unique<ipc_dispatcher>(registry_);
    }
  }

  ipc_router(const ipc_router&) = delete;
  ipc_router& operator=(const ipc_router&) = delete;

  [[nodiscard]] result bind(ipc_domain domain, decoder decode) noexcept {
    if (!dispatcher_ || decode == nullptr ||
        std::ranges::find(bindings_, domain, &binding::domain) != bindings_.end()) {
      return result::invalid_argument;
    }
    try {
      bindings_.push_back({domain, decode});
      return result::success;
    } catch (const std::bad_alloc&) {
      return result::out_of_memory;
    } catch (...) {
      return result::internal;
    }
  }

  [[nodiscard]] ipc_route_result<Message> dispatch(const ipc_envelope& envelope,
                                                   const ipc_dispatch_context& context) noexcept {
    ipc_route_result<Message> routed;
    if (!dispatcher_) {
      routed.outcome = {.rejection = ipc_dispatch_rejection::handler_failed,
                        .handler_result = result::invalid_state};
      return routed;
    }
    pending_.reset();
    routed.outcome = dispatcher_->dispatch(envelope, context);
    if (routed.outcome.accepted()) {
      routed.message = std::move(pending_);
      if (!routed.message) {
        routed.outcome = {.rejection = ipc_dispatch_rejection::handler_failed,
                          .handler_result = result::internal};
      }
    }
    pending_.reset();
    return routed;
  }

private:
  struct binding final {
    ipc_domain domain = ipc_domain::session;
    decoder decode = nullptr;
  };

  static result accept(void* context, const ipc_envelope& envelope) noexcept {
    auto& self = *static_cast<ipc_router*>(context);
    const auto match = std::ranges::find(self.bindings_, envelope.domain, &binding::domain);
    if (match == self.bindings_.end()) {
      return result::unsupported;
    }
    Message message;
    const auto operation = match->decode(envelope, message);
    if (operation == result::success) {
      self.pending_ = std::move(message);
    }
    return operation;
  }

  ipc_domain_registry registry_;
  std::unique_ptr<ipc_dispatcher> dispatcher_;
  std::vector<binding> bindings_;
  std::optional<Message> pending_;
};

/**
 * 按“协议域 + 操作”将信封交给强类型上下文处理器。
 *
 * Router 不拥有调用上下文，且 dispatch 期间不可重入；适合由单一主线程驱动的会话组合层。
 */
template <typename Context> class ipc_operation_router final {
public:
  using handler = result (*)(const ipc_envelope&, Context&) noexcept;

  ipc_operation_router() noexcept {
    const ipc_protocol_domain_handlers handlers{.session = accept,
                                                .control = accept,
                                                .log = accept,
                                                .inspection = accept,
                                                .statistics = accept,
                                                .property = accept,
                                                .context = this};
    if (register_ipc_v2_domains(handlers, registry_) == result::success) {
      dispatcher_ = std::make_unique<ipc_dispatcher>(registry_);
    }
  }

  ipc_operation_router(const ipc_operation_router&) = delete;
  ipc_operation_router& operator=(const ipc_operation_router&) = delete;

  [[nodiscard]] result bind(ipc_domain domain, std::uint16_t operation, handler handle) noexcept {
    const auto duplicate = std::ranges::find_if(bindings_, [&](const binding& value) {
      return value.domain == domain && value.operation == operation;
    });
    if (!dispatcher_ || operation == 0U || handle == nullptr || duplicate != bindings_.end()) {
      return result::invalid_argument;
    }
    try {
      bindings_.push_back({domain, operation, handle});
      return result::success;
    } catch (const std::bad_alloc&) {
      return result::out_of_memory;
    } catch (...) {
      return result::internal;
    }
  }

  [[nodiscard]] ipc_dispatch_outcome dispatch(const ipc_envelope& envelope,
                                              const ipc_dispatch_context& dispatch_context,
                                              Context& handler_context) noexcept {
    if (!dispatcher_ || pending_context_ != nullptr) {
      return {.rejection = ipc_dispatch_rejection::handler_failed,
              .handler_result = result::invalid_state};
    }
    pending_context_ = &handler_context;
    auto outcome = dispatcher_->dispatch(envelope, dispatch_context);
    pending_context_ = nullptr;
    return outcome;
  }

private:
  struct binding final {
    ipc_domain domain = ipc_domain::session;
    std::uint16_t operation = 0U;
    handler handle = nullptr;
  };

  static result accept(void* context, const ipc_envelope& envelope) noexcept {
    auto& self = *static_cast<ipc_operation_router*>(context);
    if (self.pending_context_ == nullptr) {
      return result::invalid_state;
    }
    const auto match = std::ranges::find_if(self.bindings_, [&](const binding& value) {
      return value.domain == envelope.domain && value.operation == envelope.operation;
    });
    return match == self.bindings_.end() ? result::unsupported
                                         : match->handle(envelope, *self.pending_context_);
  }

  ipc_domain_registry registry_;
  std::unique_ptr<ipc_dispatcher> dispatcher_;
  std::vector<binding> bindings_;
  Context* pending_context_ = nullptr;
};

} // namespace gneiss

#endif
