// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/tooling/import/import_ir.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace gneiss::tooling {

struct write_report {
  bool success{};
  std::string diagnostic;
};

/** 写出引擎描述与载荷；按需初始化默认 Meta，不创建 Application 或 GPU。
 * 自定义类型须在首次调用前通过 initialize_meta 注册。失败保留原输出目录。 */
[[nodiscard]] write_report write_assets(const import_ir& data,
                                        const std::filesystem::path& output_directory,
                                        std::string_view asset_uri_prefix = "asset://");

} // namespace gneiss::tooling
