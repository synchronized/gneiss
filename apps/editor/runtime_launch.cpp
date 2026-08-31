// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_launch.h"

#include <new>

namespace gneiss::editor {

runtime_launch_state inspect_runtime_launch(const editor_session& session,
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

result save_and_prepare_runtime_launch(editor_session& session,
                                       const std::filesystem::path& asset_root,
                                       const std::filesystem::path& project_root,
                                       runtime_launch_request& output) noexcept {
  output = {};
  if (!session.is_open() || session.uri().empty() || asset_root.empty() || project_root.empty()) {
    return result::invalid_state;
  }
  if (session.is_dirty()) {
    const auto operation = session.save(asset_root);
    if (operation != result::success) {
      return operation;
    }
  }
  return inspect_runtime_launch(session, project_root, output) == runtime_launch_state::ready
             ? result::success
             : result::invalid_state;
}

} // namespace gneiss::editor
