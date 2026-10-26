// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/platform/files/file_types.hpp"

#include <memory>
#include <string_view>

namespace gneiss::engine {
class file_backend;
struct mount_description {
  std::string prefix;
  std::vector<std::shared_ptr<file_backend>> readers;
  std::shared_ptr<file_backend> writer;
  /** 默认创建独立来源；共享标记仅用于相同不可变挂载配置的服务。 */
  std::shared_ptr<const file_source_domain> source{};
};
struct file_service_options {
  std::size_t max_requests{64};
  std::size_t max_file_bytes{16U * 1024U * 1024U};
  /** 在途及待分发字节缓冲预算；整读按上限预留，交付后由消费者负责。 */
  std::size_t max_buffer_bytes{256U * 1024U * 1024U};
};

/** 仅所属线程调用；后端完成数据通过同步保护转交。不可复制/移动。
 * 提交失败清空 ID 且不回调；接受后只在 dispatch_completions 分发一次终态。
 * 用户回调可提交/取消，不得递归分发或销毁服务。析构放弃通知，不等待后台。
 * 正常生命周期必须 shutdown 并持续分发至关闭回调，再停止平台执行器。 */
class GNEISS_API file_service final {
public:
  explicit file_service(file_service_options options = {});
  ~file_service();
  /** 查询挂载来源身份，不执行 I/O；失败清空输出，限所属线程。 */
  [[nodiscard]] result
  resolve_source(std::string_view uri,
                 std::shared_ptr<const file_source_domain>& output) const noexcept;
  file_service(const file_service&) = delete;
  file_service& operator=(const file_service&) = delete;
  file_service(file_service&&) = delete;
  file_service& operator=(file_service&&) = delete;

  /** 首次接受请求后禁止修改挂载，前缀以 / 结尾；最长前缀匹配。 */
  [[nodiscard]] result mount(mount_description description) noexcept;
  [[nodiscard]] result read(std::string_view uri, read_options options,
                            file_callback<file_bytes> callback, file_request_id& request) noexcept;
  [[nodiscard]] result read_range(std::string_view uri, file_range range,
                                  file_callback<file_bytes> callback,
                                  file_request_id& request) noexcept;
  [[nodiscard]] result write(std::string_view uri, file_bytes bytes, write_options options,
                             completion_callback callback, file_request_id& request) noexcept;
  [[nodiscard]] result stat(std::string_view uri, file_callback<file_info> callback,
                            file_request_id& request) noexcept;
  [[nodiscard]] result list(std::string_view uri, list_options options,
                            file_callback<directory_page> callback,
                            file_request_id& request) noexcept;
  [[nodiscard]] result flush(std::string_view mount_point, completion_callback callback,
                             file_request_id& request) noexcept;
  /** 完成先到时返回 invalid_state；取消成功仍等待实际后端结束再通知 cancelled。
   * 取消不撤销已执行的写入，ID 未知/跨服务/已分发返回 invalid_handle。 */
  [[nodiscard]] result cancel(file_request_id request) noexcept;
  /** 返回线程/重入错误；回调必须不抛出异常。本次分发不处理回调中新提交的请求。 */
  [[nodiscard]] result dispatch_completions(std::size_t max_callbacks);
  /** 取消尚未完成请求，已完成结果照常分发；关闭回调在请求全部收尾后的分发中调用。 */
  [[nodiscard]] result shutdown(completion_callback callback) noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

} // namespace gneiss::engine
