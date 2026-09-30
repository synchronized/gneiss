// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "asset/virtual_file_system.h"
#include "core/sha256.h"

#include <functional>
#include <map>
#include <mutex>

namespace gneiss::asset_internal {

/** 场景会话的只读版本账本；保存摘要而非整份负载，允许准备任务跨批次保持同 URI 一致。
 * read_bounded 与 verify 可在工作线程调用；底层 VFS 后端须支持并发只读。
 * 最终复读是变化检测，不提供跨进程文件系统的原子快照。 */
class source_revision_file_system final : public file_system {
public:
  explicit source_revision_file_system(virtual_file_system source) : source_(std::move(source)) {}
  /** 分块计算完整来源摘要后建账；不建立不可变快照，使用后仍须 verify。 */
  [[nodiscard]] gneiss_result
  open_read(std::string_view path, std::unique_ptr<read_source>& output) const noexcept override;
  gneiss_result read(std::string_view path, std::vector<std::byte>& bytes) const noexcept override;
  gneiss_result read_bounded(std::string_view path, std::size_t limit,
                             std::vector<std::byte>& bytes) const noexcept override;
  [[nodiscard]] gneiss_result verify(const std::function<bool()>& cancelled) const noexcept;
  [[nodiscard]] std::size_t source_count() const;

private:
  struct identity {
    std::uint64_t bytes{};
    core::sha256_digest digest{};
  };
  [[nodiscard]] gneiss_result remember(std::string_view path, const identity& current) const;
  virtual_file_system source_;
  mutable std::mutex mutex_;
  mutable std::map<std::string, identity> identities_;
};

} // namespace gneiss::asset_internal
