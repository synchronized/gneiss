// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/asset/virtual_file_system.hpp"
#include "engine/core/sha256.hpp"

#include <functional>
#include <map>
#include <mutex>

namespace gneiss::asset_internal {

/** 场景会话的只读版本账本；保存摘要而非整份负载，允许准备任务跨批次保持同 URI 一致。
 * read_bounded 与 verify 可在工作线程调用；底层 VFS 后端须支持并发只读。
 * 最终复读是变化检测，不提供跨进程文件系统的原子快照。 */
class source_revision_file_system final : public file_system {
public:
  /** 固定本次待复验的身份清单；由同一任务串行推进，游标独立持有 VFS 和读取来源。 */
  class verification final {
  public:
    struct statistics {
      double maximum_open_ms{};
      double maximum_read_ms{};
      double maximum_hash_ms{};
    };
    ~verification();
    /** 区间来源每步最多处理 byte_budget 字节；只支持整文件的旧后端每步最多一个文件。
     * complete 仅表示全部摘要匹配；错误为粘滞终态，取消不发布部分结果。 */
    [[nodiscard]] gneiss_result advance(std::size_t byte_budget,
                                        const std::function<bool()>& cancelled,
                                        bool& complete) noexcept;
    [[nodiscard]] std::size_t completed_sources() const noexcept;
    [[nodiscard]] statistics timings() const noexcept;

  private:
    friend class source_revision_file_system;
    struct state;
    explicit verification(std::unique_ptr<state> value);
    std::unique_ptr<state> state_;
  };
  explicit source_revision_file_system(virtual_file_system source,
                                       std::function<bool()> cancelled = {})
      : source_(std::move(source)), cancelled_(std::move(cancelled)) {}
  /** 分块计算完整来源摘要后建账；不建立不可变快照，使用后仍须 verify。 */
  [[nodiscard]] gneiss_result
  open_read(std::string_view path, std::unique_ptr<read_source>& output) const noexcept override;
  [[nodiscard]] gneiss_result
  open_read_for_validation(std::string_view path,
                           std::unique_ptr<read_source>& output) const noexcept override;
  gneiss_result read(std::string_view path, std::vector<std::byte>& bytes) const noexcept override;
  gneiss_result read_bounded(std::string_view path, std::size_t limit,
                             std::vector<std::byte>& bytes) const noexcept override;
  [[nodiscard]] gneiss_result verify(const std::function<bool()>& cancelled) const noexcept;
  [[nodiscard]] gneiss_result begin_verification(std::unique_ptr<verification>& output,
                                                 bool deferred_reads = true) const noexcept;
  [[nodiscard]] std::size_t source_count() const;

private:
  struct identity {
    std::uint64_t bytes{};
    core::sha256_digest digest{};
  };
  struct ledger {
    std::mutex mutex;
    std::map<std::string, identity> identities;
  };
  [[nodiscard]] static gneiss_result remember(const std::shared_ptr<ledger>& versions,
                                              std::string_view path, const identity& current);
  virtual_file_system source_;
  std::function<bool()> cancelled_;
  std::shared_ptr<ledger> versions_{std::make_shared<ledger>()};
};

} // namespace gneiss::asset_internal
