// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/platform/files/file_backend.hpp"
#include <memory>
#include <string>
namespace gneiss::engine {
class uv_loop_executor;
}
namespace gneiss::engine::files {
/** 桌面异步文件后端；借用执行器，须存活至所有请求收尾。
 * 根目录必须已存在；写入不创建父目录。每次成功写入均先 fsync 再关闭。
 * 路径解析拒绝根目录外的目标；不提供抵抗并发恶意符号链接替换的安全沙箱。 */
class native_backend final : public backend {
public:
  native_backend(uv_loop_executor& executor, std::string root);
  ~native_backend() override;
  error read(std::string_view, read_options, files::callback<files::bytes>) override;
  error read_range(std::string_view, range, files::callback<files::bytes>) override;
  error write(std::string_view, files::bytes, write_options, completion_callback) override;
  error stat(std::string_view, files::callback<files::info>) override;
  error list(std::string_view, list_options, files::callback<directory_page>) override;
  error flush(completion_callback) override;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};
}
