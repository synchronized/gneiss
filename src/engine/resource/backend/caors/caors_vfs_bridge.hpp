// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/platform/caors/adapter.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"

namespace gneiss::asset_internal {
/** 过渡桥：持有既有 VFS 的只读快照，后端路径为完整 URI，不另建挂载表。
 * 首阶段仅支持有界完整 CPU 文档；完整读取后登记来源摘要，最终发布复验仍由资产宿主负责。
 * worker 处理摘要和结果复制；宿主保持 io/loop 存活，串行 pump。
 * 退出时先关闭并排空 worker scope，再 abandon，最后排空 I/O。
 */
class caors_vfs_bridge : public caors_internal::file_system_adapter {
public:
  /** 透传原始 Gneiss 来源身份；不得用 CAORS 叶后端 source_id 替代挂载身份。 */
  virtual gneiss_result identify(std::string_view uri, std::uint32_t type,
                                 asset_identity& output) const noexcept = 0;
};
/** maximum_document_bytes 是单次完整读取上限，必须大于零；零值抛出 invalid_argument。
 * allow_scheduled_reads 显式允许不支持异步打开的 VFS 在 worker 执行有界同步快照读取。
 * 协作执行器将在推进队列的线程执行 I/O，不提供后台 I/O 或单步耗时保证。
 */
std::shared_ptr<caors_vfs_bridge>
make_caors_vfs_bridge(virtual_file_system snapshot, caors::tasks::executor worker,
                      std::size_t maximum_document_bytes = 1024U * 1024U,
                      bool allow_scheduled_reads = false);
} // namespace gneiss::asset_internal
