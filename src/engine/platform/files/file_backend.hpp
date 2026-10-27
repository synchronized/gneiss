// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/platform/files/file_types.hpp"

#include <memory>
#include <string_view>

namespace gneiss::engine::files {

/** 单消费者顺序来源；不预读。close 可重复调用，终止在途读取且不等待。
 * 成功接受的读取必须完成一次；失败后来源不可重试。仅所属线程调用。 */
class backend_stream {
public:
  virtual ~backend_stream() = default;
  virtual result read_next(std::size_t maximum_bytes, files::callback<block>) = 0;
  virtual void close() noexcept = 0;
};

/** 后端扩展接口；提交/poll 在服务所属线程调用，完成可来自任意线程。
 * 成功接受后恰好调用一次回调，失败不调用；回调可内联，但不包含用户代码。
 * 不得阻塞等待或进行同步磁盘/网络 I/O。后端自行保活在途数据，异步操作持有路径/缓冲。
 * 一个后端不能由多个线程同时驱动；没有实现的能力返回 unsupported。 */
class backend {
public:
  virtual ~backend() = default;
  virtual result open_stream(std::string_view, files::callback<std::shared_ptr<backend_stream>>) {
    return result::unsupported;
  }
  virtual result read(std::string_view, read_options, files::callback<files::bytes>) {
    return result::unsupported;
  }
  virtual result read_range(std::string_view, range, files::callback<files::bytes>) {
    return result::unsupported;
  }
  virtual result write(std::string_view, files::bytes, write_options, completion_callback) {
    return result::unsupported;
  }
  virtual result stat(std::string_view, files::callback<files::info>) {
    return result::unsupported;
  }
  virtual result list(std::string_view, list_options, files::callback<directory_page>) {
    return result::unsupported;
  }
  virtual result flush(completion_callback) { return result::unsupported; }
  /** 仅适配已有轮询式后端；不得同步读取文件或调用用户回调。 */
  virtual void poll() noexcept {}
};

} // namespace gneiss::engine::files
