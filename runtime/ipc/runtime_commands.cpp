// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc/runtime_commands.hpp"
#include <gneiss/engine/core/base/error.hpp>

namespace gneiss::host {

gneiss::error register_runtime_commands(runtime_command_router& router) noexcept {
  auto operation = register_runtime_asset_commands(router);
  if (operation == gneiss::error::success) {
    operation = register_runtime_scene_commands(router);
  }
  if (operation == gneiss::error::success) {
    operation = register_runtime_session_commands(router);
  }
  if (operation == gneiss::error::success) {
    operation = register_runtime_control_commands(router);
  }
  if (operation == gneiss::error::success) {
    operation = register_runtime_inspection_commands(router);
  }
  if (operation == gneiss::error::success) {
    operation = register_runtime_property_commands(router);
  }
  return operation;
}

} // namespace gneiss::host
