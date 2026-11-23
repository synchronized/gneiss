// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <gneiss/engine/core/logging/export.hpp>

#include <gneiss/engine/core/logging/logger.hpp>
#include <gneiss/engine/core/logging/sink.hpp>
#include <gneiss/engine/core/logging/sink_connection.hpp>

namespace gneiss::logging {

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
class service final {
public:
  GNEISS_LOGGING_API explicit service(service_options = {});
  GNEISS_LOGGING_API ~service();
  service(const service&) = delete;
  service& operator=(const service&) = delete;
  GNEISS_LOGGING_API logger get_logger(std::string_view channel) const;
  GNEISS_LOGGING_API sink_connection attach(std::unique_ptr<sink>, sink_options = {});
  GNEISS_LOGGING_API void set_level(level);
  GNEISS_LOGGING_API void flush();
  GNEISS_LOGGING_API void close();
  GNEISS_LOGGING_API service_metrics metrics() const noexcept;

private:
  friend class default_service_binding;
  std::shared_ptr<detail::service_state> state_;
};
}
