<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Editor 会话提取验收

日期：2026-11-22。范围：[PLAN-128 P2](../plans/PLAN-128-host-business-ownership.md)。

## 实际改动

editor::session 唯一拥有 World、文档编辑器、加载器、视口和渲染绑定。
Application 由外层拥有，会话只借用；初始化延后到服务就绪，关闭先结束文档和绑定借用再释放 World。
宿主通过会话执行打开场景、加载更新、同步、视图汇总以及取消和关闭。
UI 读取原加载器状态，不增加第二套加载成功/失败状态；面板绘制仍在原位置。

加载失败保留原文档；交互错误交给 UI，binding 错误保持原调度失败语义。
close 对在途编辑和目标背压返回 not_ready，由 on_stop 等待；成功关闭清除加载回执及视觉依赖批次。
会话对象在 Application 析构前释放。document_editing 不拥有 World 或 Render。

本阶段不注入没有实际消费者的项目工作区；会话暂与视口复用 gneiss_editor_ui 目标，
P4 再整理宿主目标边界，不把 UI 反向加入文档库。

## 验证

Windows Clang Debug 共享构建通过。八项定向 CTest 通过：

- gneiss.editor.document-editing
- gneiss.editor.scene-loader
- gneiss.editor.transform-gizmo-drag
- gneiss.editor.session
- gneiss.editor.save
- gneiss.editor.smoke
- gneiss.editor.project-manager-smoke
- gneiss.editor.scene-picking

在既有真实 GPU 消费者测试中增加会话闭环：初始化、加载场景、缺失场景失败保留文档版本、
同步与视图汇总、取消请求、关闭及重复关闭；同一 Application 内重复完整初始化与关闭两次。
复用既有文档、历史、保存和加载测试，不按新增类数量扩充测试目标。

首次扩大回归时，旧 scene-loader 可执行文件约一分钟无输出，被人工终止并记录失败；
重新编译、链接相关测试后，完整八项回归全部通过，scene-loader 用时约 0.22 秒。
未据此断言初次停滞的具体系统原因，也未跳过或放宽测试断言。

## 未验证

完整 GUI 的保存确认/取消及连续项目重开未手工执行；静态库、跨平台及完整版本矩阵未运行。
P3 Runtime 业务与 IPC 迁移、P4 工作区和目标归属尚未实施。
