// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/platform/files/file_service.hpp"
namespace gneiss::engine {
/** 按需读取；已知长度使用范围读，nullopt 使用后端顺序流，不保证文件快照。
 * 所属线程使用和销毁，最多一个在途块，不预读；消费者调用 read_next 才继续读取。
 * 持有服务引用，析构取消观察且不等待；服务仍由宿主分发和关闭。
 * 已知长度失败可原位置重试；未知长度后端读取失败或取消终止流，提交失败可重试。EOF 后不再接受读取。
 */
class GNEISS_API file_read_stream final {
public:
  file_read_stream(std::shared_ptr<file_service>, std::string uri,
                   std::optional<std::uint64_t> size, std::size_t block_bytes);
  ~file_read_stream();
  file_read_stream(const file_read_stream&) = delete;
  file_read_stream& operator=(const file_read_stream&) = delete;
  result read_next(file_callback<file_block> callback) noexcept;
  /** 取消当前块，不回退已交付位置；成功后仍经所属线程通知 cancelled。 */
  result cancel() noexcept;

private:
  result read_sequential(file_callback<file_block>) noexcept;
  struct state;
  std::shared_ptr<state> state_;
};
}
