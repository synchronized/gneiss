// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_dispatcher.h"

#include <algorithm>
#include <new>

namespace {

constexpr gneiss::ipc_message_kind_mask known_kind_mask =
    gneiss::ipc_kind_mask(gneiss::ipc_message_kind::event) |
    gneiss::ipc_kind_mask(gneiss::ipc_message_kind::request) |
    gneiss::ipc_kind_mask(gneiss::ipc_message_kind::response) |
    gneiss::ipc_kind_mask(gneiss::ipc_message_kind::error);

} // namespace

namespace gneiss {

result ipc_domain_registry::register_domain(const ipc_domain_descriptor& descriptor) noexcept {
  if (static_cast<std::uint16_t>(descriptor.domain) == 0U || descriptor.version == 0U ||
      descriptor.max_payload_size == 0U ||
      descriptor.max_payload_size > ipc_envelope_max_payload_size ||
      descriptor.handler == nullptr || descriptor.operations.empty() ||
      (descriptor.domain != ipc_domain::session && descriptor.capability.empty()) ||
      std::ranges::any_of(descriptor.operations, [](const auto& operation) {
        const auto kinds = operation.editor_to_runtime_kinds | operation.runtime_to_editor_kinds;
        return operation.operation == 0U || kinds == 0U || (kinds & ~known_kind_mask) != 0U;
      })) {
    return result::invalid_argument;
  }
  if (find(descriptor.domain) != nullptr) {
    return result::invalid_state;
  }
  for (std::size_t index = 0U; index < descriptor.operations.size(); ++index) {
    const auto operation = descriptor.operations[index].operation;
    if (std::ranges::any_of(
            descriptor.operations.subspan(index + 1U),
            [operation](const auto& candidate) { return candidate.operation == operation; })) {
      return result::invalid_argument;
    }
  }
  try {
    registered_domain registered{
        .domain = descriptor.domain,
        .version = descriptor.version,
        .capability = std::string(descriptor.capability),
        .max_payload_size = descriptor.max_payload_size,
        .operations = {descriptor.operations.begin(), descriptor.operations.end()},
        .handler = descriptor.handler,
        .handler_context = descriptor.handler_context};
    domains_.push_back(std::move(registered));
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

void ipc_domain_registry::clear() noexcept { domains_.clear(); }

std::size_t ipc_domain_registry::size() const noexcept { return domains_.size(); }

const ipc_domain_registry::registered_domain*
ipc_domain_registry::find(ipc_domain domain) const noexcept {
  const auto match = std::ranges::find(domains_, domain, &registered_domain::domain);
  return match == domains_.end() ? nullptr : &*match;
}

ipc_dispatcher::ipc_dispatcher(const ipc_domain_registry& registry) noexcept
    : registry_(&registry) {}

ipc_dispatch_outcome ipc_dispatcher::dispatch(const ipc_envelope& envelope,
                                              const ipc_dispatch_context& context) const noexcept {
  if (validate_ipc_envelope(envelope) != result::success) {
    return {.rejection = ipc_dispatch_rejection::invalid_envelope};
  }
  const auto* domain = registry_->find(envelope.domain);
  if (domain == nullptr) {
    return {.rejection = ipc_dispatch_rejection::unknown_domain};
  }
  if (envelope.payload.size() > domain->max_payload_size) {
    return {.rejection = ipc_dispatch_rejection::payload_too_large};
  }
  if (envelope.domain != ipc_domain::session) {
    if (!context.handshake_complete) {
      return {.rejection = ipc_dispatch_rejection::handshake_required};
    }
    const auto capability = std::ranges::find(context.negotiated_domains, envelope.domain,
                                              &ipc_domain_capability::domain);
    if (capability == context.negotiated_domains.end()) {
      return {.rejection = ipc_dispatch_rejection::domain_not_negotiated};
    }
    if (capability->version < domain->version) {
      return {.rejection = ipc_dispatch_rejection::unsupported_domain_version};
    }
  }
  const auto operation = std::ranges::find(domain->operations, envelope.operation,
                                           &ipc_operation_descriptor::operation);
  if (operation == domain->operations.end()) {
    return {.rejection = ipc_dispatch_rejection::unknown_operation};
  }
  const auto allowed_kinds = context.remote_role == ipc_peer_role::editor
                                 ? operation->editor_to_runtime_kinds
                                 : operation->runtime_to_editor_kinds;
  if (allowed_kinds == 0U) {
    return {.rejection = ipc_dispatch_rejection::wrong_direction};
  }
  if ((allowed_kinds & ipc_kind_mask(envelope.kind)) == 0U) {
    return {.rejection = ipc_dispatch_rejection::wrong_kind};
  }
  const auto handled = domain->handler(domain->handler_context, envelope);
  if (handled != result::success) {
    return {.rejection = ipc_dispatch_rejection::handler_failed, .handler_result = handled};
  }
  return {};
}

} // namespace gneiss
