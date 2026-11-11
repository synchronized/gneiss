// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/logging/sink.hpp"
#include "engine/core/result.hpp"
#include <filesystem>
#include <memory>

namespace gneiss::engine::logging {
/** 创建同步文件输出端。默认追加；宿主须在非实时线程调用，关闭前显式 flush。
 * 路径为空或含 NUL 返回 invalid_argument，打开失败返回 io。 */
[[nodiscard]] result<std::unique_ptr<sink>> create_file_sink(const std::filesystem::path& path,
                                                             bool truncate = false);
} // namespace gneiss::engine::logging
