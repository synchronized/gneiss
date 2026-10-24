// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_protocol_domains.hpp"
#include "engine/core/result.hpp"

#include "ipc_asset_protocol.hpp"
#include "ipc_control_protocol.hpp"
#include "ipc_inspection_protocol.hpp"
#include "ipc_log_protocol.hpp"
#include "ipc_property_protocol.hpp"
#include "ipc_scene_protocol.hpp"
#include "ipc_session_protocol.hpp"
#include "ipc_statistics_protocol.hpp"

#include <array>
#include <utility>

namespace {

constexpr std::array capabilities{gneiss::engine::platform::ipc_domain_capability{
                                      .domain = gneiss::engine::platform::ipc_domain::scene,
                                      .version = gneiss::ipc_scene_domain_version},
                                  gneiss::engine::platform::ipc_domain_capability{
                                      .domain = gneiss::engine::platform::ipc_domain::control,
                                      .version = gneiss::ipc_control_domain_version},
                                  gneiss::engine::platform::ipc_domain_capability{
                                      .domain = gneiss::engine::platform::ipc_domain::log,
                                      .version = gneiss::ipc_log_domain_version},
                                  gneiss::engine::platform::ipc_domain_capability{
                                      .domain = gneiss::engine::platform::ipc_domain::inspection,
                                      .version = gneiss::ipc_inspection_domain_version},
                                  gneiss::engine::platform::ipc_domain_capability{
                                      .domain = gneiss::engine::platform::ipc_domain::statistics,
                                      .version = gneiss::ipc_statistics_domain_version},
                                  gneiss::engine::platform::ipc_domain_capability{
                                      .domain = gneiss::engine::platform::ipc_domain::property,
                                      .version = gneiss::ipc_property_domain_version},
                                  gneiss::engine::platform::ipc_domain_capability{
                                      .domain = gneiss::engine::platform::ipc_domain::asset,
                                      .version = gneiss::ipc_asset_domain_version}};

} // namespace

namespace gneiss {

std::span<const gneiss::engine::platform::ipc_domain_capability>
ipc_v2_domain_capabilities() noexcept {
  return capabilities;
}

gneiss::engine::core::result
register_ipc_v2_domains(gneiss::engine::platform::ipc_domain_handler handler, void* handler_context,
                        gneiss::engine::platform::ipc_domain_registry& output) noexcept {
  if (handler == nullptr) {
    return gneiss::engine::core::result::invalid_argument;
  }
  gneiss::engine::platform::ipc_domain_registry registry;
  const std::array descriptors{
      gneiss::engine::platform::ipc_domain_descriptor{
          .domain = gneiss::engine::platform::ipc_domain::scene,
          .version = ipc_scene_domain_version,
          .capability = "scene",
          .max_payload_size = ipc_scene_max_payload_size,
          .operations = ipc_scene_operations(),
          .handler = handler,
          .handler_context = handler_context},
      gneiss::engine::platform::ipc_domain_descriptor{
          .domain = gneiss::engine::platform::ipc_domain::session,
          .version = ipc_session_domain_version,
          .capability = {},
          .max_payload_size = ipc_session_max_payload_size,
          .operations = ipc_session_operations(),
          .handler = handler,
          .handler_context = handler_context},
      gneiss::engine::platform::ipc_domain_descriptor{
          .domain = gneiss::engine::platform::ipc_domain::asset,
          .version = ipc_asset_domain_version,
          .capability = "asset",
          .max_payload_size = ipc_asset_max_payload_size,
          .operations = ipc_asset_operations(),
          .handler = handler,
          .handler_context = handler_context},
      gneiss::engine::platform::ipc_domain_descriptor{
          .domain = gneiss::engine::platform::ipc_domain::control,
          .version = ipc_control_domain_version,
          .capability = "control",
          .max_payload_size = ipc_control_max_payload_size,
          .operations = ipc_control_operations(),
          .handler = handler,
          .handler_context = handler_context},
      gneiss::engine::platform::ipc_domain_descriptor{
          .domain = gneiss::engine::platform::ipc_domain::log,
          .version = ipc_log_domain_version,
          .capability = "log",
          .max_payload_size = gneiss::engine::platform::ipc_envelope_max_payload_size,
          .operations = ipc_log_operations(),
          .handler = handler,
          .handler_context = handler_context},
      gneiss::engine::platform::ipc_domain_descriptor{
          .domain = gneiss::engine::platform::ipc_domain::inspection,
          .version = ipc_inspection_domain_version,
          .capability = "inspection",
          .max_payload_size = gneiss::engine::platform::ipc_envelope_max_payload_size,
          .operations = ipc_inspection_operations(),
          .handler = handler,
          .handler_context = handler_context},
      gneiss::engine::platform::ipc_domain_descriptor{
          .domain = gneiss::engine::platform::ipc_domain::statistics,
          .version = ipc_statistics_domain_version,
          .capability = "statistics",
          .max_payload_size = gneiss::engine::platform::ipc_envelope_max_payload_size,
          .operations = ipc_statistics_operations(),
          .handler = handler,
          .handler_context = handler_context},
      gneiss::engine::platform::ipc_domain_descriptor{
          .domain = gneiss::engine::platform::ipc_domain::property,
          .version = ipc_property_domain_version,
          .capability = "property",
          .max_payload_size = gneiss::engine::platform::ipc_envelope_max_payload_size,
          .operations = ipc_property_operations(),
          .handler = handler,
          .handler_context = handler_context}};
  for (const auto& descriptor : descriptors) {
    const auto operation = registry.register_domain(descriptor);
    if (operation != gneiss::engine::core::result::success) {
      return operation;
    }
  }
  output = std::move(registry);
  return gneiss::engine::core::result::success;
}

} // namespace gneiss
