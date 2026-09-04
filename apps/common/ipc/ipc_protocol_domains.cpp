// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_protocol_domains.h"

#include "ipc_control_protocol.h"
#include "ipc_data_protocol.h"
#include "ipc_session_protocol.h"

#include <array>
#include <utility>

namespace {

constexpr std::array capabilities{
    gneiss::ipc_domain_capability{.domain = gneiss::ipc_domain::control,
                                  .version = gneiss::ipc_control_domain_version},
    gneiss::ipc_domain_capability{.domain = gneiss::ipc_domain::log,
                                  .version = gneiss::ipc_log_domain_version},
    gneiss::ipc_domain_capability{.domain = gneiss::ipc_domain::inspection,
                                  .version = gneiss::ipc_inspection_domain_version},
    gneiss::ipc_domain_capability{.domain = gneiss::ipc_domain::statistics,
                                  .version = gneiss::ipc_statistics_domain_version},
    gneiss::ipc_domain_capability{.domain = gneiss::ipc_domain::property,
                                  .version = gneiss::ipc_property_domain_version}};

} // namespace

namespace gneiss {

std::span<const ipc_domain_capability> ipc_v2_domain_capabilities() noexcept {
  return capabilities;
}

result register_ipc_v2_domains(const ipc_protocol_domain_handlers& handlers,
                               ipc_domain_registry& output) noexcept {
  if (handlers.session == nullptr || handlers.control == nullptr || handlers.log == nullptr ||
      handlers.inspection == nullptr || handlers.statistics == nullptr ||
      handlers.property == nullptr) {
    return result::invalid_argument;
  }
  ipc_domain_registry registry;
  const std::array descriptors{
      ipc_domain_descriptor{.domain = ipc_domain::session,
                            .version = ipc_session_domain_version,
                            .capability = {},
                            .max_payload_size = ipc_session_max_payload_size,
                            .operations = ipc_session_operations(),
                            .handler = handlers.session,
                            .handler_context = handlers.context},
      ipc_domain_descriptor{.domain = ipc_domain::control,
                            .version = ipc_control_domain_version,
                            .capability = "control",
                            .max_payload_size = ipc_control_max_payload_size,
                            .operations = ipc_control_operations(),
                            .handler = handlers.control,
                            .handler_context = handlers.context},
      ipc_domain_descriptor{.domain = ipc_domain::log,
                            .version = ipc_log_domain_version,
                            .capability = "log",
                            .max_payload_size = ipc_envelope_max_payload_size,
                            .operations = ipc_log_operations(),
                            .handler = handlers.log,
                            .handler_context = handlers.context},
      ipc_domain_descriptor{.domain = ipc_domain::inspection,
                            .version = ipc_inspection_domain_version,
                            .capability = "inspection",
                            .max_payload_size = ipc_envelope_max_payload_size,
                            .operations = ipc_inspection_operations(),
                            .handler = handlers.inspection,
                            .handler_context = handlers.context},
      ipc_domain_descriptor{.domain = ipc_domain::statistics,
                            .version = ipc_statistics_domain_version,
                            .capability = "statistics",
                            .max_payload_size = ipc_envelope_max_payload_size,
                            .operations = ipc_statistics_operations(),
                            .handler = handlers.statistics,
                            .handler_context = handlers.context},
      ipc_domain_descriptor{.domain = ipc_domain::property,
                            .version = ipc_property_domain_version,
                            .capability = "property",
                            .max_payload_size = ipc_envelope_max_payload_size,
                            .operations = ipc_property_operations(),
                            .handler = handlers.property,
                            .handler_context = handlers.context}};
  for (const auto& descriptor : descriptors) {
    const auto operation = registry.register_domain(descriptor);
    if (operation != result::success) {
      return operation;
    }
  }
  output = std::move(registry);
  return result::success;
}

} // namespace gneiss
