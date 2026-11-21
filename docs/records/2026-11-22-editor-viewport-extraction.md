<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Editor 视口提取验收

日期：2026-11-22。范围：[PLAN-128 P1](../plans/PLAN-128-host-business-ownership.md)。

## 实际改动

新增 editor/viewport/scene_viewport，集中编辑相机、目标及旧目标、Gizmo 模式/拖动/等待释放、拾取请求。
删除 editor_state 的对应字段和重复 view_camera 快照。加载器通过借用相机维持原取景行为。
UI 保留布局和输入采集，Gizmo 实际绘制仍复用既有绘制函数，视口负责提供其状态并协调拾取优先级。

同步由 Application synchronize system 执行：World 发布变化、binding 同步成功、视口处理拾取。
视口不更新 World，也不自行提交普通帧。关闭目标遇到背压由 on_stop 等待重试。
World、文档和 binding 所有权未改动；Editor 会话提取仍属于 P2。

新实现与拾取、Gizmo 拖动归 gneiss_editor_ui，后两者已从文档库移出。
相关测试改为链接实际生产目标，未增加新的静态库或重复编译生产源码。

## 验证结果

Windows Clang Debug 共享配置：Editor、新视口消费者与 Gizmo 测试构建通过。
六项 CTest 全部通过：

- gneiss.editor.scene-picking
- gneiss.editor.transform-gizmo-view
- gneiss.editor.transform-gizmo-drag
- gneiss.editor.camera
- gneiss.editor.smoke
- gneiss.editor.project-manager-smoke

复用原拾取测试验证新视口的真实 GPU 命中/背景、Gizmo 优先、相机取景、缩放取消旧请求，
以及关闭释放当前和旧目标、重复关闭。既有过期文档/相机/选择保护继续通过。

## 限制

未执行手工高 DPI、多显示器和连续交互验收；未运行本轮静态库及跨平台矩阵。
本组不代表 PLAN-128 全部完成，也不替代 PLAN-115 的完整 GPU 故障验收。
