// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/platform/files/backend.hpp>
namespace gneiss::files {
/** 易失文件后端，写入隐式创建父目录；不支持持久化 flush。 */
[[nodiscard]] GNEISS_API std::shared_ptr<backend> create_backend_memory();
} // namespace gneiss::files
