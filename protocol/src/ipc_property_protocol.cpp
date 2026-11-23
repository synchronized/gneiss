// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/protocol/ipc_property_protocol.hpp>
#include <gneiss/protocol/payload.hpp>

namespace gneiss::host {
namespace {
constexpr std::size_t max_json_size = 64U * 1024U;
bool valid_value(const gneiss::value& value) {
  const auto* text = std::get_if<std::string>(&value.payload);
  return !text || text->size() <= 16U * 1024U;
}
bool valid(const ipc_property_write& command) {
  return command.session_id != 0 && command.object.is_valid() && !command.type_id.is_nil() &&
         command.field_id != 0 && command.expected_revision != 0 && valid_value(command.value);
}
bool valid(const ipc_property_write_result& response) {
  return response.session_id != 0 && response.message.size() <= 1024U &&
         (response.code != 0 ||
          (response.revision != 0 &&
           !std::holds_alternative<std::monostate>(response.canonical_value.payload) &&
           valid_value(response.canonical_value)));
}
}
const protocol::message_descriptor<ipc_property_write> property_write{
    protocol::domain::property,
    static_cast<std::uint16_t>(ipc_property_operation::write),
    ipc::message_kind::request,
    protocol::peer_role::editor,
    max_json_size,
    valid};
const protocol::message_descriptor<ipc_property_write_result> property_result{
    protocol::domain::property,
    static_cast<std::uint16_t>(ipc_property_operation::write),
    ipc::message_kind::response,
    protocol::peer_role::runtime,
    max_json_size,
    valid};

std::span<const gneiss::protocol::operation_descriptor> ipc_property_operations() noexcept {
  static const std::array operations = [] {
    auto rule = property_write.rule();
    rule.runtime_to_editor_kinds |= property_result.rule().runtime_to_editor_kinds;
    return std::array{rule};
  }();
  return operations;
}

} // namespace gneiss::host
