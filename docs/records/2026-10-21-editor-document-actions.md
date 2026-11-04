<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Editor 未保存确认流程拆分

日期：2026-10-21。按[模块布局计划](../plans/PLAN-051-module-layout.md)实施。

## 变更

将宿主 pending_document_action 枚举状态迁入 documents/document_actions，未保存确认
弹窗迁入 panels/document_action_panel。删除宿主中的重复弹窗实现，Host 仅以同步回调
连接实际保存和场景/退出动作。保留单槽替换语义，不引入动作队列或脏状态副本。
关闭请求统一调用 document_editing 的 active/cancel，避免分别维护三类取消分支。

## 验证

Windows Clang Debug 全量构建通过，17 项相关测试通过。新增测试覆盖干净文档直接执行、
脏文档等待确认、保存失败与 not_ready 保留请求、保存成功只返回一次动作、取消与放弃、
单槽替换。现有文档生命周期、保存、场景加载、资产/Prefab 编辑及工作流回归通过。
格式检查和 git diff --check 通过。

未运行 GUI 人工点击验收或远端矩阵。场景加载进度、Save and Run、其他菜单/面板装配
仍在宿主，不能把本批视为 Editor Application 整体拆分完成。
