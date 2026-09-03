// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_AUTHOR_TRANSACTION_H_
#define GNEISS_APPS_EDITOR_AUTHOR_TRANSACTION_H_

#include <gneiss/core/result.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::editor {

struct author_revision final {
  std::uint64_t digest = 0U;
  std::uint64_t byte_size = 0U;

  [[nodiscard]] bool operator==(const author_revision&) const noexcept = default;
};

struct author_document final {
  std::string path;
  std::optional<std::string> contents;
};

struct author_document_change final {
  std::string path;
  std::optional<std::string> baseline;
  std::optional<std::string> replacement;
};

/**
 * 跨作者文档事务的内存参考模型。
 *
 * 完整内容用于冲突判断，revision 只用于诊断。对象仅供 Editor 主线程使用；不具备线程安全性。
 */
class author_transaction final {
public:
  /** 添加一项文档变更；路径必须非空且不能重复。 */
  [[nodiscard]] result add(author_document_change change) noexcept;
  /** 校验全部基线；失败时不修改文档。 */
  [[nodiscard]] result prepare(std::span<const author_document> documents) noexcept;
  /**
   * 提交全部变更。failure_before 用于 Spike 故障注入；命中时恢复已应用项并返回 I/O 错误。
   */
  [[nodiscard]] result commit(std::vector<author_document>& documents,
                              std::size_t failure_before = static_cast<std::size_t>(-1)) noexcept;
  /** 当前内容仍等于提交结果时恢复全部基线。 */
  [[nodiscard]] result undo(std::vector<author_document>& documents) noexcept;
  /** 当前内容仍等于基线时再次应用全部变更。 */
  [[nodiscard]] result redo(std::vector<author_document>& documents) noexcept;

  [[nodiscard]] std::size_t size() const noexcept { return changes_.size(); }
  [[nodiscard]] author_revision baseline_revision(std::size_t index) const noexcept;

private:
  enum class state { draft, prepared, committed, undone };

  [[nodiscard]] result validate(std::span<const author_document> documents,
                                bool expect_replacement) const noexcept;
  [[nodiscard]] result apply(std::vector<author_document>& documents, bool use_replacement,
                             std::size_t failure_before) noexcept;

  std::vector<author_document_change> changes_;
  state state_ = state::draft;
};

[[nodiscard]] author_revision make_author_revision(std::string_view contents) noexcept;

} // namespace gneiss::editor

#endif
