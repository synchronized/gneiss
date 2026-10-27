// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_COMMAND_HISTORY_H_
#define GNEISS_APPS_EDITOR_EDITOR_COMMAND_HISTORY_H_

#include "engine/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::editor {

/** 所属线程推进的历史事务；失败或取消完成前必须恢复原文档，终态后才允许销毁。
 * 未完成返回 success 并设置 complete=false；失败仅表示已完成回滚的终态。
 * 取消须继续推进清理，以非 success 终态结束，不得留下待清理工作。
 */
class editor_history_operation {
public:
  virtual ~editor_history_operation() noexcept = default;
  [[nodiscard]] virtual gneiss::engine::result advance(bool& complete) noexcept = 0;
  virtual void cancel() noexcept = 0;
  [[nodiscard]] virtual bool can_cancel() const noexcept { return true; }
};

/** 可逆 Editor 命令；实现只保存稳定作者值，不得保存 Runtime 对象地址。 */
class editor_command {
public:
  virtual ~editor_command() noexcept = default;
  [[nodiscard]] virtual std::string_view label() const noexcept = 0;
  [[nodiscard]] virtual gneiss::engine::result undo() noexcept = 0;
  [[nodiscard]] virtual gneiss::engine::result redo() noexcept = 0;
  /** 创建分帧撤销/重做事务；空输出沿用同步入口。准备失败不得修改文档。 */
  [[nodiscard]] virtual gneiss::engine::result
  begin_history_operation(bool undo, std::unique_ptr<editor_history_operation>& output) noexcept {
    (void)undo;
    output.reset();
    return gneiss::engine::result::success;
  }
  /** 合并已执行的后继命令；成功时保留本命令的初始 Undo 状态。 */
  [[nodiscard]] virtual bool merge_with(const editor_command& next) noexcept {
    (void)next;
    return false;
  }
};

/** 函数命令适配器；merge_key 相同且非空时合并连续编辑。 */
class functional_editor_command final : public editor_command {
public:
  using operation = std::function<gneiss::engine::result()>;
  struct desc final {
    std::string label;
    operation undo;
    operation redo;
    std::string merge_key;
  };

  explicit functional_editor_command(desc value) noexcept;
  [[nodiscard]] bool is_valid() const noexcept { return undo_ && redo_; }
  [[nodiscard]] std::string_view label() const noexcept override { return label_; }
  [[nodiscard]] gneiss::engine::result undo() noexcept override;
  [[nodiscard]] gneiss::engine::result redo() noexcept override;
  [[nodiscard]] bool merge_with(const editor_command& next) noexcept override;

private:
  std::string label_;
  operation undo_;
  operation redo_;
  std::string merge_key_;
};

/** 保存可逆命令、保存点及有限历史；命令失败时保持当前位置。
 * 所属线程使用；销毁前必须取消并推进在途操作至终态，不在析构中阻塞等待。
 */
class editor_command_history final {
public:
  using command = functional_editor_command::desc;
  static constexpr std::size_t default_capacity = 256U;

  explicit editor_command_history(std::size_t capacity = default_capacity) noexcept;
  /** 记录已经成功执行的命令；新命令清空 redo。 */
  [[nodiscard]] gneiss::engine::result record(std::unique_ptr<editor_command> value) noexcept;
  /** 异步初次执行前预留记录空间；调用方期间禁止其他历史修改。 */
  [[nodiscard]] gneiss::engine::result prepare_record() noexcept;
  [[nodiscard]] gneiss::engine::result record(command value) noexcept;
  /** 执行 redo 后记录；记录失败时自动调用 undo 回滚。 */
  [[nodiscard]] gneiss::engine::result execute(std::unique_ptr<editor_command> value) noexcept;
  [[nodiscard]] gneiss::engine::result execute(command value) noexcept;
  [[nodiscard]] gneiss::engine::result undo() noexcept;
  [[nodiscard]] gneiss::engine::result redo() noexcept;
  /** 在途操作完成清理前拒绝清空或改变保存点。 */
  gneiss::engine::result clear(bool saved = true) noexcept;
  gneiss::engine::result mark_saved() noexcept {
    if (is_pending()) {
      return gneiss::engine::result::not_ready;
    }
    saved_state_ = current_state_;
    return gneiss::engine::result::success;
  }
  [[nodiscard]] bool is_pending() const noexcept { return pending_ != nullptr; }
  [[nodiscard]] gneiss::engine::result advance_pending() noexcept;
  void cancel_pending() noexcept;
  [[nodiscard]] bool can_cancel_pending() const noexcept {
    return pending_ && pending_->can_cancel();
  }

  [[nodiscard]] bool can_undo() const noexcept { return !is_pending() && cursor_ != 0U; }
  [[nodiscard]] bool can_redo() const noexcept {
    return !is_pending() && cursor_ < entries_.size();
  }
  [[nodiscard]] bool is_dirty() const noexcept { return current_state_ != saved_state_; }
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::string_view undo_label() const noexcept;
  [[nodiscard]] std::string_view redo_label() const noexcept;

private:
  struct entry final {
    std::unique_ptr<editor_command> value;
    std::uint64_t before_state = 0U;
    std::uint64_t after_state = 0U;
  };

  [[nodiscard]] gneiss::engine::result record_impl(std::unique_ptr<editor_command>& value) noexcept;
  [[nodiscard]] std::uint64_t allocate_state() noexcept;

  std::vector<entry> entries_;
  std::unique_ptr<editor_history_operation> pending_;
  bool pending_undo_{};
  std::size_t cursor_ = 0U;
  std::size_t capacity_ = default_capacity;
  std::uint64_t next_state_ = 1U;
  std::uint64_t current_state_ = 0U;
  std::uint64_t saved_state_ = 0U;
};

} // namespace gneiss::editor

#endif
