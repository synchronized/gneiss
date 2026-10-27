// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_launch.hpp"
#include "engine/core/result.hpp"

namespace gneiss::editor {

runtime_launch_state inspect_runtime_launch(const scene_document& session,
                                            const std::filesystem::path& project_root,
                                            runtime_launch_request& output) noexcept {
  output = {};
  if (!session.is_open() || project_root.empty()) {
    return runtime_launch_state::blocked;
  }
  if (session.is_dirty() || session.uri().empty()) {
    return runtime_launch_state::requires_save;
  }
  try {
    output.project_root = project_root.lexically_normal();
    return runtime_launch_state::ready;
  } catch (...) {
    output = {};
    return runtime_launch_state::blocked;
  }
}

} // namespace gneiss::editor
