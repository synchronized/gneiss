// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/platform/files/file_backend.hpp"
#include <memory>
namespace gneiss::engine::files {
enum class browser_storage { memory, persistent, existing };

/** 浏览器主线程的 MEMFS/IDBFS 后端；existing 借用宿主已有目录，不挂载或卸载它。
 * initialize 完成后可访问；写入仅修改内存，flush 成功才确认 IndexedDB 持久化。
 * 同步持久化期间拒绝本后端访问，避免写入与恢复交错；失败可重试 flush。
 * 父目录须存在。析构不自动持久化，宿主须显式 flush 后再关闭服务。 */
class browser_backend final : public backend {
public:
  browser_backend(std::string root, browser_storage storage);
  ~browser_backend() override;
  result initialize(completion_callback);
  result read(std::string_view, read_options, files::callback<files::bytes>) override;
  result read_range(std::string_view, range, files::callback<files::bytes>) override;
  result write(std::string_view, files::bytes, write_options, completion_callback) override;
  result stat(std::string_view, files::callback<files::info>) override;
  result list(std::string_view, list_options, files::callback<directory_page>) override;
  result flush(completion_callback) override;

private:
  struct implementation;
  std::shared_ptr<implementation> impl_;
};
}
