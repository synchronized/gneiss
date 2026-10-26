// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/platform/files/file_backend.hpp"
#include <memory>
#include <string>
namespace gneiss::engine {
class uv_loop_executor;
/** 桌面异步文件后端；借用执行器，须存活至所有请求收尾。
 * 根目录必须已存在；写入不创建父目录。每次成功写入均先 fsync 再关闭。
 * 路径解析拒绝根目录外的目标；不提供抵抗并发恶意符号链接替换的安全沙箱。 */
class native_file_backend final : public file_backend {
public:
  native_file_backend(uv_loop_executor& executor, std::string root);
  ~native_file_backend() override;
  result read(std::string_view, read_options, file_callback<file_bytes>) override;
  result read_range(std::string_view, file_range, file_callback<file_bytes>) override;
  result write(std::string_view, file_bytes, write_options, completion_callback) override;
  result stat(std::string_view, file_callback<file_info>) override;
  result list(std::string_view, list_options, file_callback<directory_page>) override;
  result flush(completion_callback) override;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};
}
