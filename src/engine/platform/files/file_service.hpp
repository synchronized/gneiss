// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/platform/files/file_types.hpp"

#include <memory>
#include <string_view>

namespace gneiss::engine::files {
class backend;
class backend_stream;
class read_stream;
struct mount_description {
  std::string prefix;
  std::vector<std::shared_ptr<backend>> readers;
  std::shared_ptr<backend> writer;
  /** 默认创建独立来源；共享标记仅用于相同不可变挂载配置的服务。 */
  std::shared_ptr<const source_domain> source{};
};
struct service_options {
  std::size_t max_requests{64};
  std::size_t max_file_bytes{16U * 1024U * 1024U};
  /** 在途及待分发字节缓冲预算；整读按上限预留，交付后由消费者负责。 */
  std::size_t max_buffer_bytes{256U * 1024U * 1024U};
};

/** 仅所属线程调用；后端完成数据通过同步保护转交。不可复制/移动。
 * 提交失败清空 ID 且不回调；接受后只在 dispatch_completions 分发一次终态。
 * 用户回调可提交/取消，不得递归分发或销毁服务。析构放弃通知，不等待后台。
 * 正常生命周期必须 shutdown 并持续分发至关闭回调，再停止平台执行器。 */
class GNEISS_API service final {
public:
  explicit service(service_options options = {});
  ~service();
  /** 查询挂载来源身份，不执行 I/O；失败清空输出，限所属线程。 */
  [[nodiscard]] error resolve_source(std::string_view uri,
                                     std::shared_ptr<const source_domain>& output) const noexcept;
  service(const service&) = delete;
  service& operator=(const service&) = delete;
  service(service&&) = delete;
  service& operator=(service&&) = delete;

  /** 首次接受请求后禁止修改挂载，前缀以 / 结尾；最长前缀匹配。 */
  [[nodiscard]] error mount(mount_description description) noexcept;
  /** 使用服务配置的单文件上限；需要更严格限制时使用 read_options 重载。 */
  [[nodiscard]] error read(std::string_view uri, files::callback<files::bytes> callback,
                           request_id& request) noexcept;
  [[nodiscard]] error read(std::string_view uri, read_options options,
                           files::callback<files::bytes> callback, request_id& request) noexcept;
  [[nodiscard]] error read_range(std::string_view uri, range range,
                                 files::callback<files::bytes> callback,
                                 request_id& request) noexcept;
  [[nodiscard]] error write(std::string_view uri, files::bytes bytes, write_options options,
                            completion_callback callback, request_id& request) noexcept;
  [[nodiscard]] error stat(std::string_view uri, files::callback<files::info> callback,
                           request_id& request) noexcept;
  [[nodiscard]] error list(std::string_view uri, list_options options,
                           files::callback<directory_page> callback, request_id& request) noexcept;
  [[nodiscard]] error flush(std::string_view mount_point, completion_callback callback,
                            request_id& request) noexcept;
  /** 完成先到时返回 invalid_state；取消成功仍等待实际后端结束再通知 cancelled。
   * 取消不撤销已执行的写入，ID 未知/跨服务/已分发返回 invalid_handle。 */
  [[nodiscard]] error cancel(request_id request) noexcept;
  /** 返回线程/重入错误；回调必须不抛出异常。本次分发不处理回调中新提交的请求。 */
  [[nodiscard]] error dispatch_completions(std::size_t max_callbacks);
  /** 取消尚未完成请求，已完成结果照常分发；关闭回调在请求全部收尾后的分发中调用。 */
  [[nodiscard]] error shutdown(completion_callback callback) noexcept;

private:
  friend class read_stream;
  error open_stream_source(std::string_view, std::size_t,
                           files::callback<std::shared_ptr<backend_stream>>, request_id&) noexcept;
  error read_stream_source(std::string_view, std::shared_ptr<backend_stream>, std::size_t,
                           files::callback<block>, request_id&) noexcept;
  struct implementation;
  std::unique_ptr<implementation> impl_;
};

} // namespace gneiss::engine::files
