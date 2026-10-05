<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：场景会话与宿主目标拆分

日期：2026-10-05。

## 依赖核对与实施

源码核对确认 `editor_session` 类本身只使用 Engine、场景 URI 和资产根路径，
不使用工程描述或 IPC；原有依赖来自同名 CMake 目标混编了工程、进程与协议实现。
本轮无需增加描述对象、回调或另一份会话状态。

将场景会话和 Gizmo 拖拽的四个源码文件迁至 `src/editor/`，头改为 `.hpp`。
`gneiss_editor_session` 现在仅依赖 `gneiss_editor_model`（及其 Engine 依赖）。
原 Apps 侧工程、进程、IPC 与运行时同步目标更名为 `gneiss_editor_host`，依赖独立会话库。
更新所有目标使用方；保存与拖拽测试直接链接会话，Gizmo 输入回放直接链接模型。
现有 session 综合测试包含运行时同步，仍通过宿主目标验证。

## 验证结果

- Windows Clang Debug 共享配置全量增量构建通过。
- Editor、工程/IPC 与边界 CTest 49/49 通过，49.40 秒。
- 四个迁移源码除 include 路径外内容一致，clang-format 检查通过。
- 两个会话实现的实际编译命令不含仓库 Apps 路径；保存和拖拽测试的链接命令不含宿主、工程或 IPC 协议库。
- 修改文档相对链接与提交前 git diff --check 通过。
- 公共头、ABI 和安装目标未修改；本轮未重跑静态、完整 Engine 单测及跨平台 Actions。

## 剩余工作

工程与 IPC 协议、运行时同步、主程序面板编排仍在 Apps，完整薄入口尚未完成。
不把场景会话的独立构建描述为全部宿主功能已迁移；SDK 审计及最终发布矩阵仍待完成。
