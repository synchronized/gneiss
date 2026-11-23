// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/function/files/export.hpp>
#include <gneiss/engine/function/files/backend.hpp>
namespace gneiss::files {
struct fetch_config {
  std::string base_url;
};
/** 浏览器主线程只读 HTTP 来源；基址以 / 结尾。 */
[[nodiscard]] GNEISS_FILES_API std::shared_ptr<backend> create_backend_fetch(fetch_config config);
} // namespace gneiss::files
