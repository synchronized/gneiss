// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <optional>
#include <string>
namespace gneiss::editor {
/** 内存编辑计划的一项；不表示跨文件提交或磁盘撤销。 */
struct author_document_change final {
  std::string path;
  std::optional<std::string> baseline;
  std::optional<std::string> replacement;
};
}
