// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/platform/files/file_backend.hpp"
namespace gneiss::engine::files {
/** 浏览器主线程使用；只读 HTTP 后端，不写入 IDBFS。
 * 服务基址以 / 结尾；Range 必须返回匹配的 206/Content-Range，绝不整读模拟范围。 */
class fetch_backend final : public backend {
public:
  explicit fetch_backend(std::string base_url);
  error open_stream(std::string_view, files::callback<std::shared_ptr<backend_stream>>) override;
  error read(std::string_view, read_options, files::callback<files::bytes>) override;
  error read_range(std::string_view, range, files::callback<files::bytes>) override;
  error stat(std::string_view, files::callback<files::info>) override;

private:
  std::string base_url_;
};
}
