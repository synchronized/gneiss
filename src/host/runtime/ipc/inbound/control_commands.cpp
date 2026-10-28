// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "ipc/runtime_commands.hpp"

#include "ipc_control_protocol.hpp"

namespace gneiss::host {
namespace {

gneiss::engine::error validate(const gneiss::engine::ipc_envelope& envelope,
                               ipc_control_operation expected) noexcept {
  ipc_control_operation decoded = ipc_control_operation::invalid;
  const auto operation = decode_ipc_control_request(envelope, decoded);
  return operation == gneiss::engine::error::success && decoded != expected
             ? gneiss::engine::error::invalid_argument
             : operation;
}

gneiss::engine::error handle_pause(const gneiss::engine::ipc_envelope& envelope,
                                   runtime_command_context& context) noexcept {
  const auto decoded = validate(envelope, ipc_control_operation::pause);
  if (decoded != gneiss::engine::error::success) {
    return decoded;
  }
  if (context.state() != runtime_ipc_state::running) {
    (void)context.send_protocol_error(gneiss::engine::error::invalid_state, "当前状态不能暂停",
                                      envelope.request_id);
    return gneiss::engine::error::success;
  }
  context.set_state(runtime_ipc_state::paused);
  context.actions().pause_game = true;
  return context.send_state(ipc_control_state::paused);
}

gneiss::engine::error handle_resume(const gneiss::engine::ipc_envelope& envelope,
                                    runtime_command_context& context) noexcept {
  const auto decoded = validate(envelope, ipc_control_operation::resume);
  if (decoded != gneiss::engine::error::success) {
    return decoded;
  }
  if (context.state() != runtime_ipc_state::paused) {
    (void)context.send_protocol_error(gneiss::engine::error::invalid_state, "当前状态不能恢复",
                                      envelope.request_id);
    return gneiss::engine::error::success;
  }
  context.set_state(runtime_ipc_state::running);
  context.actions().resume_game = true;
  return context.send_state(ipc_control_state::running);
}

gneiss::engine::error handle_stop(const gneiss::engine::ipc_envelope& envelope,
                                  runtime_command_context& context) noexcept {
  const auto decoded = validate(envelope, ipc_control_operation::stop);
  if (decoded != gneiss::engine::error::success || context.state() == runtime_ipc_state::stopping) {
    return decoded;
  }
  context.set_state(runtime_ipc_state::stopping);
  context.actions().request_exit = true;
  return context.send_state(ipc_control_state::stopping);
}

} // namespace

gneiss::engine::error register_runtime_control_commands(runtime_command_router& router) noexcept {
  auto operation =
      router.bind(gneiss::engine::ipc_domain::control,
                  static_cast<std::uint16_t>(ipc_control_operation::pause), handle_pause);
  if (operation == gneiss::engine::error::success) {
    operation =
        router.bind(gneiss::engine::ipc_domain::control,
                    static_cast<std::uint16_t>(ipc_control_operation::resume), handle_resume);
  }
  if (operation == gneiss::engine::error::success) {
    operation = router.bind(gneiss::engine::ipc_domain::control,
                            static_cast<std::uint16_t>(ipc_control_operation::stop), handle_stop);
  }
  return operation;
}

} // namespace gneiss::host
