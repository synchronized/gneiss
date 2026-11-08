// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/scene_document_snapshot.hpp"
#include <memory>
#include <string>
#include <vector>

namespace gneiss::editor {
/** 文档的不可变内容、版本标识和保存点；不访问 World、文件或执行命令回调。
 * 由文档所属线程使用。宿主在接受编辑后单独同步运行时，同步失败不撤销版本。
 */
class scene_document final {
public:
  struct revision {
    std::shared_ptr<const std::byte> owner, state;
    bool operator==(const revision&) const = default;
  };
  class version final {
  public:
    version() noexcept = default;
    explicit operator bool() const noexcept { return content_ != nullptr; }
    /** 借用不可变内容；调用方必须持有有效 version。 */
    const scene_document_snapshot& content() const noexcept { return *content_; }
    bool operator==(const version& other) const noexcept { return stamp() == other.stamp(); }
    revision stamp() const noexcept { return {owner_, state_}; }

  private:
    friend class scene_document;
    std::shared_ptr<const std::byte> owner_, state_;
    std::shared_ptr<const scene_document_snapshot> content_;
  };

  scene_document() noexcept = default;
  /** 创建短期编辑候选；共享不可变基线，候选变化不发布到原文档。 */
  [[nodiscard]] engine::result<std::unique_ptr<scene_document>> make_candidate() const noexcept;
  scene_document(const scene_document&) = delete;
  scene_document& operator=(const scene_document&) = delete;
  /** 校验初始内容；成功才替换整个文档，旧回执随之失效。 */
  [[nodiscard]] engine::error reset(const scene_document_snapshot&, bool saved = true) noexcept;
  /** expected 必须仍为当前版本。结构非法或分配失败不改变当前内容。 */
  [[nodiscard]] engine::error accept(const version& expected,
                                     const scene_document_snapshot&) noexcept;
  /** 切回同一文档的已接受版本；不重新解码，不创建第二条历史。 */
  [[nodiscard]] engine::error checkout(const version& expected, const version& target) noexcept;
  /** 用局部历史重建目标内容；轻量标识不保留整份旧文档。 */
  [[nodiscard]] engine::error checkout(const revision& expected, const revision& target,
                                       const scene_document_snapshot&) noexcept;
  [[nodiscard]] version current() const noexcept { return current_; }
  [[nodiscard]] bool is_dirty() const noexcept;
  /** 显式使保存点失效；普通编辑通过版本变化自动变脏。 */
  void mark_dirty() noexcept { saved_.reset(); }
  /** 保存完成时传回实际写出的版本；后续编辑保持 dirty，其他文档的回执被拒绝。 */
  [[nodiscard]] engine::error mark_saved(const version&) noexcept;

private:
  version current_;
  std::weak_ptr<const std::byte> saved_;
};
}
