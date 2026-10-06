<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：独立编辑模型与宿主边界

日期：2026-10-05。

## 依赖审计与实施

`apps/common` 的工程描述含构建配置、模块路径与启动信息，IPC 含 Editor/Runtime 会话语义。
不将这些实现整体下沉 Core/Platform，也不让 Runtime 链接 Editor。拆分顺序补入 ADR-054。

从原编辑会话目标抽出相机、网格、旋转与 Gizmo 数学、命令历史、属性检查、创作事务、
本机文件事务及 Prefab 创作的 18 个文件，迁至 `src/editor/`，内部头改为 `.hpp`。
建立 `gneiss_editor_model` 内部静态目标，仅链接 Engine 与私有 yyjson。
Session 依赖该模型库并复用唯一实现；八个模型测试目标改为直接链接模型库。

模型的实际编译命令不含仓库 Apps 路径。运行时逻辑不变；逐文件排除 include 与空白后内容一致。
顺带修正 prefab_authoring.cpp 一处既有参数排版，未改算法。公共接口和安装目标保持不变。

## 验证结果

- Windows Clang Debug 共享配置全量增量构建通过。
- Editor、工程/IPC 与边界 CTest 49/49 通过，47.08 秒。
- 18 个源码排除 include 与空白后的比对、clang-format 检查通过。
- 九个实现的实际编译命令不含仓库 Apps 路径。
- 修改文档相对链接与提交前 git diff --check 通过。
- 本轮未重跑静态、全量 Engine 单测及跨平台 Actions。

## 剩余边界

Gizmo 拖拽仍依赖会话；会话、工程描述、IPC 及主程序面板编排继续拆分。
本轮没有将整个 Session 移入 src，也没有通过添加 Apps 包含目录掩盖反向依赖。
完整薄入口、SDK 审计和最终跨平台矩阵仍未完成。
