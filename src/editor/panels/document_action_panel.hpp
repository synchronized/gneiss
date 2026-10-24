// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "documents/document_actions.hpp"
#include "engine/core/result.hpp"
namespace gneiss::editor {
/** 回调仅在本次绘制内同步调用，面板不保存 context；两项回调均必需。 */
struct document_action_callbacks final {
  void* context = nullptr;
  gneiss::engine::result (*save)(void*) = nullptr;
  void (*perform)(void*, document_action) = nullptr;
};
/** 未保存确认弹窗；保存失败保持打开，成功或放弃后只派发一次动作。 */
void draw_document_action_panel(document_actions& actions,
                                const document_action_callbacks& callbacks);
}
