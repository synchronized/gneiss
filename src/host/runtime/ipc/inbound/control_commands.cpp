// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "ipc/runtime_commands.hpp"

#include "ipc_control_protocol.hpp"

namespace gneiss::host {
namespace {

gneiss::engine::result validate(const gneiss::engine::ipc_envelope& envelope,
                                ipc_control_operation expected) noexcept {
  ipc_control_operation decoded = ipc_control_operation::invalid;
  const auto operation = decode_ipc_control_request(envelope, decoded);
  return operation == gneiss::engine::result::success && decoded != expected
             ? gneiss::engine::result::invalid_argument
             : operation;
}

gneiss::engine::result handle_pause(const gneiss::engine::ipc_envelope& envelope,
                                    runtime_command_context& context) noexcept {
  const auto decoded = validate(envelope, ipc_control_operation::pause);
  if (decoded != gneiss::engine::result::success) {
    return decoded;
  }
  if (context.state() != runtime_ipc_state::running) {
    (void)context.send_protocol_error(gneiss::engine::result::invalid_state, "当前状态不能暂停",
                                      envelope.request_id);
    return gneiss::engine::result::success;
  }
  context.set_state(runtime_ipc_state::paused);
  context.actions().pause_game = true;
  return context.send_state(ipc_control_state::paused);
}

gneiss::engine::result handle_resume(const gneiss::engine::ipc_envelope& envelope,
                                     runtime_command_context& context) noexcept {
  const auto decoded = validate(envelope, ipc_control_operation::resume);
  if (decoded != gneiss::engine::result::success) {
    return decoded;
  }
  if (context.state() != runtime_ipc_state::paused) {
    (void)context.send_protocol_error(gneiss::engine::result::invalid_state, "当前状态不能恢复",
                                      envelope.request_id);
    return gneiss::engine::result::success;
  }
  context.set_state(runtime_ipc_state::running);
  context.actions().resume_game = true;
  return context.send_state(ipc_control_state::running);
}

gneiss::engine::result handle_stop(const gneiss::engine::ipc_envelope& envelope,
                                   runtime_command_context& context) noexcept {
  const auto decoded = validate(envelope, ipc_control_operation::stop);
  if (decoded != gneiss::engine::result::success ||
      context.state() == runtime_ipc_state::stopping) {
    return decoded;
  }
  context.set_state(runtime_ipc_state::stopping);
  context.actions().request_exit = true;
  return context.send_state(ipc_control_state::stopping);
}

} // namespace

gneiss::engine::result register_runtime_control_commands(runtime_command_router& router) noexcept {
  auto operation =
      router.bind(gneiss::engine::ipc_domain::control,
                  static_cast<std::uint16_t>(ipc_control_operation::pause), handle_pause);
  if (operation == gneiss::engine::result::success) {
    operation =
        router.bind(gneiss::engine::ipc_domain::control,
                    static_cast<std::uint16_t>(ipc_control_operation::resume), handle_resume);
  }
  if (operation == gneiss::engine::result::success) {
    operation = router.bind(gneiss::engine::ipc_domain::control,
                            static_cast<std::uint16_t>(ipc_control_operation::stop), handle_stop);
  }
  return operation;
}

} // namespace gneiss::host
