// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/platform/caors/adapter.hpp"
#include "virtual_file_system.hpp"

namespace gneiss::asset_internal {
/** 过渡桥：持有既有 VFS 的只读快照，后端路径为完整 URI，不另建挂载表。
 * 首阶段仅支持有界完整 CPU 文档；完整读取后登记来源摘要，最终发布复验仍由资产宿主负责。
 * 宿主保持既有 io/loop 存活，串行 pump，退出前 abandon 并排空。
 */
class caors_vfs_bridge : public caors_internal::file_system_adapter {
public:
  /** 透传原始 Gneiss 来源身份；不得用 CAORS 叶后端 source_id 替代挂载身份。 */
  virtual gneiss_result identify(std::string_view uri, std::uint32_t type,
                                 asset_identity& output) const noexcept = 0;
};
std::shared_ptr<caors_vfs_bridge> make_caors_vfs_bridge(virtual_file_system snapshot);
} // namespace gneiss::asset_internal
