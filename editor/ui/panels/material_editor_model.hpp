// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/commands/author_file_save.hpp"
#include "documents/commands/editor_command_history.hpp"
#include <gneiss/engine/core/meta/instance.hpp>
#include <gneiss/engine/resource/material/material_description.hpp>
#include "ui/panels/property_path.hpp"
namespace gneiss::editor {
using material_property_path = property_path;
/** 单个材质文件的主线程编辑状态；只拥有描述和快照，不修改资源缓存或 GPU。
 * 历史只保存描述快照；异步回调只持有文件回执。对象不可复制或移动。 */
class material_editor_model final {
public:
  material_editor_model() = default;
  material_editor_model(const material_editor_model&) = delete;
  material_editor_model& operator=(const material_editor_model&) = delete;
  gneiss::error open(std::string uri, std::string content);
  gneiss::error set_value(const material_property_path&, const gneiss::value&,
                          std::uint64_t edit_serial = 0) noexcept;
  gneiss::error replace_object(const property_path&, const gneiss::meta::const_instance&) noexcept;
  gneiss::error undo() { return history_.undo(); }
  gneiss::error redo() { return history_.redo(); }
  bool can_undo() const noexcept { return history_.can_undo(); }
  bool can_redo() const noexcept { return history_.can_redo(); }
  /** 丢弃未保存修改；保存期间不允许丢弃或切换文件。 */
  gneiss::error discard() noexcept;
  gneiss::error begin_save(gneiss::files::service&);
  void advance();
  bool saving() const noexcept { return receipt_ != nullptr; }
  gneiss::error save_status() const noexcept { return save_status_; }
  bool is_dirty() const noexcept;
  bool is_open() const noexcept { return current_ != nullptr; }
  std::string_view uri() const noexcept { return uri_; }
  /** 借用至下一次修改；调用方不得跨刷新保存反射视图。 */
  const gneiss::material_description& description() const noexcept { return current_->description; }

private:
  gneiss::error apply(const property_path&, const gneiss::value*,
                      const gneiss::meta::const_instance*, std::uint64_t);
  struct snapshot {
    gneiss::material_description description;
    std::string encoded;
  };
  std::string uri_;
  std::shared_ptr<const snapshot> current_, saved_, pending_;
  std::shared_ptr<const std::string> baseline_;
  std::shared_ptr<author_file_save> receipt_;
  gneiss::error save_status_{gneiss::error::success};
  editor_command_history history_;
};
}
