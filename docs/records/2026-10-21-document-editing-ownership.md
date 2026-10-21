<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 文档编辑所有权收敛验收

日期：2026-10-21。

按 [布局计划](../plans/PLAN-051-module-layout.md) 和
[资源与运行时边界决策](../decisions/ADR-069-resource-runtime-boundaries.md)，
新增 document_editing，拥有既有文档、历史、资产编辑和 Prefab 编辑对象。
Editor 宿主改为持有该对象；退出取消与清理推进集中，执行器仍由宿主驱动。
没有新增任务队列、重复操作状态或公共 API。

## 验证

- Windows Clang Debug 全量构建通过。
- 16 项相关测试通过：文档编辑生命周期、Prefab 编辑、资产编辑、场景加载、Gizmo、
  文档、保存、历史、Prefab 作者操作、资产工作流、两个灯柱工作流、属性编辑、
  核心边界及负例、内部头文件可见性。
- 新生命周期测试验证：在途历史操作拒绝关闭且保持原场景；取消后继续排空；
  历史命令在文档关闭之前销毁；重复关闭成功。
- git diff --check 通过。

## 限制

未运行交互式 GUI 人工验收，也未运行远端矩阵。场景切换、保存提示、请求互斥和
正常帧 UI 更新顺序保持原流程；Editor 宿主整体迁移未完成。
内部接口要求宿主线程使用，调用方仍须保证 Application 与执行器存活至清理完成。
