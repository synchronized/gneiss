// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/platform/files/file_backend.hpp"

#include <map>

namespace gneiss::engine::files {
/** 所属线程使用的易失文件后端；写入隐式建立父目录，不支持持久化 flush。
 * 不使用 std::filesystem，适用于桌面/浏览器和一致性测试。 */
class GNEISS_API memory_backend final : public backend {
public:
  error read(std::string_view path, read_options options,
             files::callback<files::bytes> callback) override;
  error read_range(std::string_view path, range range,
                   files::callback<files::bytes> callback) override;
  error write(std::string_view path, files::bytes bytes, write_options options,
              completion_callback callback) override;
  error stat(std::string_view path, files::callback<files::info> callback) override;
  error list(std::string_view path, list_options options,
             files::callback<directory_page> callback) override;

private:
  std::map<std::string, files::bytes, std::less<>> files_;
};
} // namespace gneiss::engine::files
