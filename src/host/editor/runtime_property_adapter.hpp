// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "ipc_property_protocol.hpp"
#include "runtime_property_edits.hpp"

#include <utility>

namespace gneiss::editor {

/** 宿主消费协议值；移动字符串及数组进入模型。 */
[[nodiscard]] inline runtime_property_value
to_runtime_property_value(ipc_property_value&& value) noexcept {
  return {std::move(value.payload)};
}

/** 传输字段只在宿主映射，模型不持有协议对象。 */
[[nodiscard]] inline ipc_property_write
to_ipc_property_write(runtime_property_write&& value) noexcept {
  return {.session_id = value.session_id,
          .command_id = value.command_id,
          .object = {value.object.value, value.object.generation},
          .type_id = value.type_id,
          .field_id = value.field_id,
          .expected_revision = value.expected_revision,
          .value = {std::move(value.value.payload)}};
}

[[nodiscard]] inline runtime_property_write_result
to_runtime_property_result(ipc_property_write_result&& value) noexcept {
  return {.session_id = value.session_id,
          .command_id = value.command_id,
          .code = value.code,
          .revision = value.revision,
          .message = std::move(value.message),
          .canonical_value = to_runtime_property_value(std::move(value.canonical_value))};
}

} // namespace gneiss::editor
