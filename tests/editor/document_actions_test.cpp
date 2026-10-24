// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "documents/document_actions.hpp"
#include <initializer_list>

int main() {
  using namespace gneiss;
  using editor::document_action;
  editor::document_actions actions;
  if (actions.pending() ||
      actions.request(document_action::new_scene, false) != document_action::new_scene ||
      actions.pending())
    return 1;
  if (actions.request(document_action::open_scene, true) != document_action::none ||
      !actions.pending())
    return 2;
  // 保存出错、用户取消文件对话框或异步工作尚未完成，都不能丢掉待执行动作。
  for (auto status : {gneiss::engine::core::result::io, gneiss::engine::core::result::not_ready,
                      gneiss::engine::core::result::invalid_state}) {
    if (actions.saved(status) != document_action::none || !actions.pending())
      return 3;
  }
  if (actions.saved(gneiss::engine::core::result::success) != document_action::open_scene ||
      actions.pending() ||
      actions.saved(gneiss::engine::core::result::success) != document_action::none)
    return 4;
  actions.defer(document_action::exit_editor);
  actions.cancel();
  if (actions.pending() || actions.discard() != document_action::none)
    return 5;
  actions.defer(document_action::exit_editor);
  if (actions.discard() != document_action::exit_editor || actions.pending() ||
      actions.discard() != document_action::none)
    return 6;
  // 保留原来的单槽替换语义，不另建动作队列。
  (void)actions.request(document_action::new_scene, true);
  (void)actions.request(document_action::open_scene, true);
  if (actions.request(document_action::none, true) != document_action::none ||
      actions.saved(gneiss::engine::core::result::success) != document_action::open_scene)
    return 7;
  return 0;
}
