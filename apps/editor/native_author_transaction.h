// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_NATIVE_AUTHOR_TRANSACTION_H_
#define GNEISS_APPS_EDITOR_NATIVE_AUTHOR_TRANSACTION_H_

#include "author_transaction.h"

#include <cstddef>
#include <filesystem>
#include <limits>
#include <span>

namespace gneiss::editor {

struct native_author_transaction_options final {
  /** 仅供故障恢复测试：替换指定数量目标后模拟进程中断并保留恢复目录。 */
  std::size_t interrupt_after_replacements = std::numeric_limits<std::size_t>::max();
  /** 仅供故障恢复测试：写入提交标记后模拟进程中断。 */
  bool interrupt_after_commit_marker = false;
};

/**
 * 在 Native FileSystem 资产根内提交多项作者文档变更。
 *
 * 路径使用相对资产根的 UTF-8 文本。函数仅供 Editor 主线程调用；失败时保留可恢复证据或恢复旧值。
 */
[[nodiscard]] result
commit_native_author_transaction(const std::filesystem::path& asset_root,
                                 std::span<const author_document_change> changes,
                                 const native_author_transaction_options& options = {}) noexcept;

/** 在打开作者文档前恢复资产根内遗留的未完成事务。 */
[[nodiscard]] result
recover_native_author_transactions(const std::filesystem::path& asset_root) noexcept;

} // namespace gneiss::editor

#endif
