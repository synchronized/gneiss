// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/dispatcher.hpp>

#include <algorithm>
#include <new>

namespace {

constexpr gneiss::protocol::message_kind_mask known_kind_mask =
    gneiss::protocol::kind_mask(gneiss::ipc::message_kind::event) |
    gneiss::protocol::kind_mask(gneiss::ipc::message_kind::request) |
    gneiss::protocol::kind_mask(gneiss::ipc::message_kind::response) |
    gneiss::protocol::kind_mask(gneiss::ipc::message_kind::error);

} // namespace

namespace gneiss::protocol {

error dispatcher::register_domain(const domain_descriptor& descriptor) {
  if (static_cast<std::uint16_t>(descriptor.domain) == 0U || descriptor.version == 0U ||
      descriptor.max_payload_size == 0U ||
      descriptor.max_payload_size > ipc::envelope_max_payload_size ||
      descriptor.handler == nullptr || descriptor.operations.empty() ||
      (descriptor.domain != domain::session && descriptor.capability.empty()) ||
      std::ranges::any_of(descriptor.operations, [](const auto& operation) {
        const auto kinds = operation.editor_to_runtime_kinds | operation.runtime_to_editor_kinds;
        return operation.operation == 0U || kinds == 0U || (kinds & ~known_kind_mask) != 0U;
      })) {
    return error::invalid_argument;
  }
  if (find(descriptor.domain) != nullptr) {
    return error::invalid_state;
  }
  for (std::size_t index = 0U; index < descriptor.operations.size(); ++index) {
    const auto operation = descriptor.operations[index].operation;
    if (std::ranges::any_of(
            descriptor.operations.subspan(index + 1U),
            [operation](const auto& candidate) { return candidate.operation == operation; })) {
      return error::invalid_argument;
    }
  }
  {
    registered_domain registered{
        .domain = descriptor.domain,
        .version = descriptor.version,
        .capability = std::string(descriptor.capability),
        .max_payload_size = descriptor.max_payload_size,
        .operations = {descriptor.operations.begin(), descriptor.operations.end()},
        .handler = descriptor.handler,
        .handler_context = descriptor.handler_context};
    domains_.push_back(std::move(registered));
    return error::success;
  }
}

void dispatcher::clear() noexcept { domains_.clear(); }

std::size_t dispatcher::size() const noexcept { return domains_.size(); }

const dispatcher::registered_domain* dispatcher::find(std::uint16_t domain) const noexcept {
  const auto match = std::ranges::find(domains_, domain, &registered_domain::domain);
  return match == domains_.end() ? nullptr : &*match;
}

dispatch_outcome dispatcher::validate(const ipc::envelope& envelope,
                                      const dispatch_context& context) const {
  if (ipc::validate_envelope(envelope) != error::success) {
    return {.rejection = dispatch_rejection::invalid_envelope};
  }
  const auto* domain = find(envelope.domain);
  if (domain == nullptr) {
    return {.rejection = dispatch_rejection::unknown_domain};
  }
  if (envelope.payload.size() > domain->max_payload_size) {
    return {.rejection = dispatch_rejection::payload_too_large};
  }
  if (envelope.domain != domain::session) {
    if (!context.handshake_complete) {
      return {.rejection = dispatch_rejection::handshake_required};
    }
    const auto capability =
        std::ranges::find(context.negotiated_domains, envelope.domain, &domain_capability::domain);
    if (capability == context.negotiated_domains.end()) {
      return {.rejection = dispatch_rejection::domain_not_negotiated};
    }
    if (capability->version < domain->version) {
      return {.rejection = dispatch_rejection::unsupported_domain_version};
    }
  }
  const auto operation =
      std::ranges::find(domain->operations, envelope.operation, &operation_descriptor::operation);
  if (operation == domain->operations.end()) {
    return {.rejection = dispatch_rejection::unknown_operation};
  }
  const auto allowed_kinds = context.remote_role == peer_role::editor
                                 ? operation->editor_to_runtime_kinds
                                 : operation->runtime_to_editor_kinds;
  if (allowed_kinds == 0U) {
    return {.rejection = dispatch_rejection::wrong_direction};
  }
  if ((allowed_kinds & kind_mask(envelope.kind)) == 0U) {
    return {.rejection = dispatch_rejection::wrong_kind};
  }
  return {};
}

dispatch_outcome dispatcher::dispatch(const ipc::envelope& envelope,
                                      const dispatch_context& context) const {
  auto outcome = validate(envelope, context);
  if (!outcome.accepted())
    return outcome;
  const auto* domain = find(envelope.domain);
  const auto handled = domain->handler(domain->handler_context, envelope);
  if (handled != error::success) {
    return {.rejection = dispatch_rejection::handler_failed, .handler_result = handled};
  }
  return {};
}

} // namespace gneiss::protocol
