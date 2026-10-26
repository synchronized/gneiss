// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/platform/files/file_types.hpp"

#include <string_view>

namespace gneiss::engine {

/** 后端扩展接口；提交/poll 在服务所属线程调用，完成可来自任意线程。
 * 成功接受后恰好调用一次回调，失败不调用；回调可内联，但不包含用户代码。
 * 不得阻塞等待或进行同步磁盘/网络 I/O。后端自行保活在途数据，异步操作持有路径/缓冲。
 * 一个后端不能由多个线程同时驱动；没有实现的能力返回 unsupported。 */
class file_backend {
public:
  virtual ~file_backend() = default;
  virtual result read(std::string_view, read_options, file_callback<file_bytes>) {
    return result::unsupported;
  }
  virtual result read_range(std::string_view, file_range, file_callback<file_bytes>) {
    return result::unsupported;
  }
  virtual result write(std::string_view, file_bytes, write_options, completion_callback) {
    return result::unsupported;
  }
  virtual result stat(std::string_view, file_callback<file_info>) { return result::unsupported; }
  virtual result list(std::string_view, list_options, file_callback<directory_page>) {
    return result::unsupported;
  }
  virtual result flush(completion_callback) { return result::unsupported; }
  /** 仅适配已有轮询式后端；不得同步读取文件或调用用户回调。 */
  virtual void poll() noexcept {}
};

} // namespace gneiss::engine
