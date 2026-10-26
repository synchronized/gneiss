// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/platform/files/file_service.hpp"
namespace gneiss::engine {
struct file_block {
  file_bytes bytes;
  std::uint64_t offset{};
  bool eof{};
};
/** 对已知长度文件按需读取；长度通常来自异步 stat，不保证文件快照。
 * 所属线程使用，最多一个在途块，不预读；消费者调用 read_next 才继续读取。
 * 持有服务引用，析构取消观察且不等待；服务仍由宿主分发和关闭。
 * 成功交付块后推进位置，失败允许原位置重试；EOF 后不再接受读取。 */
class GNEISS_API file_read_stream final {
public:
  file_read_stream(std::shared_ptr<file_service>, std::string uri, std::uint64_t size,
                   std::size_t block_bytes);
  ~file_read_stream();
  file_read_stream(const file_read_stream&) = delete;
  file_read_stream& operator=(const file_read_stream&) = delete;
  result read_next(file_callback<file_block> callback) noexcept;
  /** 取消当前块，不回退已交付位置；成功后仍经所属线程通知 cancelled。 */
  result cancel() noexcept;

private:
  struct state;
  std::shared_ptr<state> state_;
};
}
