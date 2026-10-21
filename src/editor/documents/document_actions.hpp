// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <gneiss/engine/core/result.hpp>

namespace gneiss::editor {
enum class document_action { none, new_scene, open_scene, exit_editor };
/** 单文档的未保存确认状态；只在 Editor 线程使用，不拥有文档或宿主。
 * 取代宿主 pending 枚举，不存储脏状态副本，也不执行 I/O。 */
class document_actions final {
public:
  /** 干净文档返回可执行动作；需要确认时仅记录动作。 */
  [[nodiscard]] document_action request(document_action action, bool dirty) noexcept;
  /** 在途编辑取消后的退出请求仍按原流程等待用户确认。 */
  void defer(document_action action) noexcept;
  [[nodiscard]] bool pending() const noexcept { return pending_ != document_action::none; }
  /** 保存失败或尚未完成时保留请求；成功后仅返回一次动作。 */
  [[nodiscard]] document_action saved(result operation) noexcept;
  [[nodiscard]] document_action discard() noexcept;
  void cancel() noexcept;

private:
  document_action pending_ = document_action::none;
};
}
