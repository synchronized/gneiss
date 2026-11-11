// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/logging/sink.hpp"
#include "engine/core/result.hpp"
#include <filesystem>
#include <memory>

namespace gneiss::logging {
/** 创建同步文件输出端。默认追加；宿主须在非实时线程调用，关闭前显式 flush。
 * 路径为空或含 NUL 返回 invalid_argument，打开失败返回 io。 */
[[nodiscard]] result<std::unique_ptr<sink>> create_file_sink(const std::filesystem::path& path,
                                                             bool truncate = false);
/** 同步轮转输出，达到容量时保留指定数量的备份；备份命名遵循 spdlog。 */
[[nodiscard]] result<std::unique_ptr<sink>>
create_rotating_file_sink(const std::filesystem::path&, std::size_t maximum_size = 1024 * 1024,
                          std::size_t backup_count = 1);
} // namespace gneiss::logging
