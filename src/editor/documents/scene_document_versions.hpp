// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/scene_document_snapshot.hpp"
#include <memory>
#include <string>
#include <vector>

namespace gneiss::editor {
/** 文档的不可变版本、保存点和历史；不访问 World、文件或执行命令回调。
 * 由文档所属线程使用。宿主在接受编辑后单独同步运行时，同步失败不撤销版本。
 */
class scene_document_versions final {
public:
  class version final {
  public:
    version() noexcept = default;
    explicit operator bool() const noexcept { return content_ != nullptr; }
    /** 借用不可变内容；调用方必须持有有效 version。 */
    const scene_document_snapshot& content() const noexcept { return *content_; }
    bool operator==(const version&) const noexcept = default;

  private:
    friend class scene_document_versions;
    std::shared_ptr<const std::byte> owner_;
    std::shared_ptr<const scene_document_snapshot> content_;
  };

  explicit scene_document_versions(std::size_t capacity = 256) noexcept : capacity_(capacity) {}
  scene_document_versions(const scene_document_versions&) = delete;
  scene_document_versions& operator=(const scene_document_versions&) = delete;
  /** 校验初始内容；成功才替换整个文档及历史，旧回执随之失效。 */
  [[nodiscard]] engine::error reset(const scene_document_snapshot&, bool saved = true) noexcept;
  /** expected 必须仍为当前版本。结构非法或分配失败不改变当前内容和历史。 */
  [[nodiscard]] engine::error accept(const version& expected, const scene_document_snapshot&,
                                     std::string label, std::string merge_key = {}) noexcept;
  /** 切回同一文档的已接受版本；不重新解码，不创建第二条历史。 */
  [[nodiscard]] engine::error checkout(const version& expected, const version& target) noexcept;
  [[nodiscard]] engine::error undo() noexcept;
  [[nodiscard]] engine::error redo() noexcept;
  void clear_history() noexcept;
  [[nodiscard]] version current() const noexcept { return current_; }
  [[nodiscard]] bool can_undo() const noexcept { return cursor_ != 0; }
  [[nodiscard]] bool can_redo() const noexcept { return cursor_ < entries_.size(); }
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] std::string_view undo_label() const noexcept;
  [[nodiscard]] std::string_view redo_label() const noexcept;
  [[nodiscard]] bool is_dirty() const noexcept;
  /** 保存完成时传回实际写出的版本；后续编辑保持 dirty，其他文档的回执被拒绝。 */
  [[nodiscard]] engine::error mark_saved(const version&) noexcept;
  /** 仅接收当前版本的同步结果；过期完成不得把新版本误标为已同步。 */
  [[nodiscard]] engine::error report_projection(const version&, engine::error) noexcept;
  [[nodiscard]] bool is_synchronized() const noexcept;
  [[nodiscard]] engine::error projection_status() const noexcept { return projection_status_; }

private:
  struct entry {
    version before, after;
    std::string label, merge_key;
  };
  version current_;
  std::weak_ptr<const scene_document_snapshot> saved_, synchronized_;
  std::vector<entry> entries_;
  std::size_t cursor_{};
  std::size_t capacity_;
  engine::error projection_status_{engine::error::not_ready};
  void changed() noexcept;
};
}
