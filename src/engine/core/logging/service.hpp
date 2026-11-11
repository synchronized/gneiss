// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/logging/logger.hpp"
#include "engine/core/logging/sink.hpp"
#include "engine/core/logging/sink_connection.hpp"
namespace gneiss::engine::logging {
enum class dispatch_mode { synchronous, asynchronous };
struct service_options {
  dispatch_mode mode = dispatch_mode::asynchronous;
  std::size_t queue_capacity = 1024;
  level minimum_level = level::info;
};
struct sink_options {
  level minimum_level = level::trace;
};
struct service_metrics {
  std::uint64_t output_failures{};
};
class default_service_binding;
/** 独占日志服务。生命周期操作由宿主串行调用；关闭前停止生产者。
 * Sink 不得重入 flush/close；默认 Logger 与连接均不保活服务。 */
class GNEISS_API service final {
public:
  explicit service(service_options = {});
  ~service();
  service(const service&) = delete;
  service& operator=(const service&) = delete;
  logger get_logger(std::string_view channel) const;
  sink_connection attach(std::unique_ptr<sink>, sink_options = {});
  void set_level(level);
  void flush();
  void close();
  service_metrics metrics() const noexcept;

private:
  friend class default_service_binding;
  std::shared_ptr<detail::service_state> state_;
};
}
