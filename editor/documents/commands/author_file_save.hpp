// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/commands/author_document_change.hpp"
#include <gneiss/engine/platform/files/file_service.hpp>
namespace gneiss::editor {
struct author_file_save {
  gneiss::error status{gneiss::error::not_ready};
  bool complete{};
};
/** 单来源异步保存；先检查已有内容，再安全写入，不承诺跨进程比较交换。
 * 回执不借用编辑器；服务须由宿主持续分发。没有删除或跨文件回滚。 */
[[nodiscard]] gneiss::error begin_author_file_save(gneiss::files::service& files,
                                                   author_document_change change,
                                                   std::shared_ptr<author_file_save>& output);
}
