// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_commands.h"

namespace gneiss::runtime_internal {

result register_runtime_commands(runtime_command_router& router) noexcept {
  auto operation = register_runtime_asset_commands(router);
  if (operation == result::success) {
    operation = register_runtime_session_commands(router);
  }
  if (operation == result::success) {
    operation = register_runtime_control_commands(router);
  }
  if (operation == result::success) {
    operation = register_runtime_inspection_commands(router);
  }
  if (operation == result::success) {
    operation = register_runtime_property_commands(router);
  }
  return operation;
}

} // namespace gneiss::runtime_internal
