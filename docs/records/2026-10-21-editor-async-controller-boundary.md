<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Editor 异步控制器与宿主边界迁移

日期：2026-10-21。依据：[模块布局计划](../plans/PLAN-051-module-layout.md)、ADR-069。

## 交付

将 editor_scene_loader 移入 src/editor/documents，将 editor_asset_edit 与 editor_prefab_edit
移入 src/editor/commands；旧 Apps 头与实现删除，无兼容转发。
三者编译归属由 gneiss_editor_host 改为 gneiss_scene_document；对应流程测试直接链接
编辑器模块和任务库，不再借助宿主库传递进程、项目或 IPC 依赖。
这些实现现在受 src/editor 的禁止反向包含 Apps/宿主协议规则检查。

没有新增运行时对象或复制流程状态。加载控制器处理候选完成后的文档切换；资产/Prefab
控制器负责准备、提交及取消清理，成功操作沿用既有历史命令。
宿主继续拥有控制器、文档和历史，并负责在 Application/执行器销毁前取消、排空及释放。

## 验证与限制

Windows Clang Debug 完整构建通过；14 项定向回归全部通过，涵盖三类异步控制器、文档保存、撤销历史、
Prefab 作者操作、资产/灯笼工作流、内部头可见性和依赖正反例。
原始结果见本地 build/editor-controllers-tests.log，构建日志为 build/editor-controllers-build.log。
没有修改公共 ABI/安装接口，没有运行图形窗口、跨平台或远端 Actions。

Editor Application 尚未迁入 src/editor；模态 UI、操作互斥与关闭编排仍在宿主，
本批只消除了三个独立编辑流程对宿主构建目标的依赖，不代表整体布局完成。
