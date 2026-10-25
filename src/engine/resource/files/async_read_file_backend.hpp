// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/resource/files/file_backend.hpp"

#include <memory>

namespace gneiss::engine {
class file_system;
/** 将现有异步文件后端接入回调服务，仅允许 begin_open/begin_read/异步 close。
 * 迁移期用于已配置 libuv 的 native_file_system；禁止同步回退。
 * 平台执行器必须持续运行至服务关闭完成，不支持写/stat/list/flush。 */
class GNEISS_API async_read_file_backend final : public file_backend {
public:
  explicit async_read_file_backend(std::shared_ptr<file_system> backend);
  ~async_read_file_backend() override;
  result read(std::string_view path, read_options options,
              file_callback<file_bytes> callback) override;
  result read_range(std::string_view path, file_range range,
                    file_callback<file_bytes> callback) override;
  void poll() noexcept override;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};
} // namespace gneiss::engine
