// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/function/files/export.hpp>
#include <gneiss/engine/function/files/backend.hpp>
namespace gneiss::files {
enum class browser_storage { memory, persistent, existing };
struct browser_config {
  std::string root;
  browser_storage storage{browser_storage::memory};
};
/** 创建不挂载；initialize 完成恢复后可用，持久化必须显式 flush。 */
[[nodiscard]] GNEISS_FILES_API std::shared_ptr<backend> create_backend_browser(browser_config config);
} // namespace gneiss::files
