// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/platform/files/file_backend.hpp"
namespace gneiss::engine {
/** 浏览器主线程使用；只读 HTTP 后端，不写入 IDBFS。
 * 服务基址以 / 结尾；Range 必须返回匹配的 206/Content-Range，绝不整读模拟范围。 */
class fetch_file_backend final : public file_backend {
public:
  explicit fetch_file_backend(std::string base_url);
  result open_stream(std::string_view,
                     file_callback<std::shared_ptr<file_backend_stream>>) override;
  result read(std::string_view, read_options, file_callback<file_bytes>) override;
  result read_range(std::string_view, file_range, file_callback<file_bytes>) override;
  result stat(std::string_view, file_callback<file_info>) override;

private:
  std::string base_url_;
};
}
