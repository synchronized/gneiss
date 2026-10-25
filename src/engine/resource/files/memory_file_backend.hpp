// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/resource/files/file_backend.hpp"

#include <map>

namespace gneiss::engine {
/** 所属线程使用的易失文件后端；写入隐式建立父目录，不支持持久化 flush。
 * 不使用 std::filesystem，适用于桌面/浏览器和一致性测试。 */
class memory_file_backend final : public file_backend {
public:
  result read(std::string_view path, read_options options,
              file_callback<file_bytes> callback) override;
  result read_range(std::string_view path, file_range range,
                    file_callback<file_bytes> callback) override;
  result write(std::string_view path, file_bytes bytes, write_options options,
               completion_callback callback) override;
  result stat(std::string_view path, file_callback<file_info> callback) override;
  result list(std::string_view path, list_options options,
              file_callback<directory_page> callback) override;

private:
  std::map<std::string, file_bytes, std::less<>> files_;
};
} // namespace gneiss::engine
