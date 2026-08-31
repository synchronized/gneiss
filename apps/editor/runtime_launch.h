// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_RUNTIME_LAUNCH_H_
#define GNEISS_APPS_EDITOR_RUNTIME_LAUNCH_H_

#include "editor_session.h"

#include <filesystem>

namespace gneiss::editor {

enum class runtime_launch_state { blocked, requires_save, ready };

/** 传给独立 Runtime 宿主的值对象，不借用 Editor 的场景、World 或撤销状态。 */
struct runtime_launch_request final {
  std::filesystem::path project_root;
};

/** 检查作者会话是否可以从磁盘启动；脏场景必须先显式保存。 */
[[nodiscard]] runtime_launch_state inspect_runtime_launch(const editor_session& session,
                                                          const std::filesystem::path& project_root,
                                                          runtime_launch_request& output) noexcept;

/** 保存脏场景并生成仅包含工程根的 Runtime 启动请求。 */
[[nodiscard]] result save_and_prepare_runtime_launch(editor_session& session,
                                                     const std::filesystem::path& asset_root,
                                                     const std::filesystem::path& project_root,
                                                     runtime_launch_request& output) noexcept;

} // namespace gneiss::editor

#endif
