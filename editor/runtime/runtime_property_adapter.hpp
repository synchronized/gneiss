// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/protocol/ipc_property_protocol.hpp>
#include "inspection/runtime_property_edits.hpp"

#include <utility>

namespace gneiss::host {

/** 传输字段只在宿主映射，模型不持有协议对象。 */
[[nodiscard]] inline ipc_property_write
to_ipc_property_write(gneiss::editor::runtime_property_write&& value) noexcept {
  return {.session_id = value.session_id,
          .command_id = value.command_id,
          .object = {value.object.value, value.object.generation},
          .type_id = value.type_id,
          .field_id = value.field_id,
          .expected_revision = value.expected_revision,
          .value = std::move(value.value)};
}

[[nodiscard]] inline gneiss::editor::runtime_property_write_result
to_runtime_property_result(ipc_property_write_result&& value) noexcept {
  return {.session_id = value.session_id,
          .command_id = value.command_id,
          .code = value.code,
          .revision = value.revision,
          .message = std::move(value.message),
          .canonical_value = std::move(value.canonical_value)};
}

} // namespace gneiss::host
