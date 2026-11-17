// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/ipc_scene_protocol.hpp>
#include <gneiss/protocol/payload.hpp>

#include <array>
#include <utility>

namespace gneiss::host {
namespace {
bool valid_source(const ipc_scene_request& value) {
  return value.session != 0U && value.revision != 0U && value.uri.starts_with("asset://") &&
         value.uri.size() > 8U && value.uri.size() <= 2048U &&
         value.uri.find("..") == std::string::npos && value.uri.find('\0') == std::string::npos;
}
bool valid_progress(const ipc_scene_progress& value) {
  return valid_source(value.source) && value.phase <= ipc_scene_phase::cancelled &&
         value.total <= 65536U && value.completed <= value.total && value.message.size() <= 4096U &&
         (!value.can_cancel || !scene_phase_terminal(value.phase)) &&
         (!value.budget || !value.budget->cleanup_pending || !value.budget->cleanup_complete);
}
}
const protocol::message_descriptor<ipc_scene_request> scene_load{
    protocol::domain::scene,    static_cast<std::uint16_t>(ipc_scene_operation::load),
    ipc::message_kind::request, protocol::peer_role::editor,
    ipc_scene_max_payload_size, valid_source};
const protocol::message_descriptor<ipc_scene_request> scene_cancel{
    protocol::domain::scene,    static_cast<std::uint16_t>(ipc_scene_operation::cancel),
    ipc::message_kind::event,   protocol::peer_role::editor,
    ipc_scene_max_payload_size, valid_source};
const protocol::message_descriptor<ipc_scene_progress> scene_progress{
    protocol::domain::scene,    static_cast<std::uint16_t>(ipc_scene_operation::progress),
    ipc::message_kind::event,   protocol::peer_role::runtime,
    ipc_scene_max_payload_size, valid_progress};
const protocol::message_descriptor<ipc_scene_progress> scene_result{
    protocol::domain::scene,
    static_cast<std::uint16_t>(ipc_scene_operation::load),
    ipc::message_kind::response,
    protocol::peer_role::runtime,
    ipc_scene_max_payload_size,
    [](const ipc_scene_progress& value) {
      return valid_progress(value) && scene_phase_terminal(value.phase);
    }};
bool apply_scene_cleanup_update(ipc_scene_progress& current,
                                const ipc_scene_progress& incoming) noexcept {
  if (current.phase != ipc_scene_phase::cancelled || incoming.phase != current.phase ||
      current.source.session != incoming.source.session ||
      current.source.revision != incoming.source.revision ||
      current.source.uri != incoming.source.uri || !current.budget ||
      !current.budget->cleanup_pending || !incoming.budget || !incoming.budget->cleanup_complete ||
      incoming.budget->cleanup_pending) {
    return false;
  }
  current.budget = incoming.budget;
  return true;
}
bool scene_phase_terminal(ipc_scene_phase phase) noexcept {
  return phase == ipc_scene_phase::applied || phase == ipc_scene_phase::failed ||
         phase == ipc_scene_phase::cancelled;
}
std::span<const gneiss::protocol::operation_descriptor> ipc_scene_operations() noexcept {
  static const std::array operations = [] {
    auto load = scene_load.rule();
    load.runtime_to_editor_kinds |= scene_result.rule().runtime_to_editor_kinds;
    return std::array{load, scene_cancel.rule(), scene_progress.rule()};
  }();
  return operations;
}
} // namespace gneiss::host
