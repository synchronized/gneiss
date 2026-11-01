<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 平台对象命名统一验收

日期：2026-10-21。按 [布局计划](../plans/PLAN-051-module-layout.md) 实施。

child_process、dynamic_library 以及 IPC 传输、信封和分发对象迁入
`gneiss::engine::platform`，调用方使用新名称，不保留兼容别名。
Windows、POSIX 和无平台实现同步调整声明；Apps 域协议和负载未迁入 Engine。
内部边界检查增加旧平台对象名称负例，不改变公共 C ABI、网络格式或对象所有权。

## 验证

- Windows Clang Debug 全量构建通过。
- 22 项相关测试通过：IPC 信封/分发/传输、8 项宿主协议、游戏模块与运行时 IPC、
  项目游戏模块、编辑器 IPC、子进程、编辑器运行进程/场景加载、动态库、核心边界及负例。
- git diff --check 通过。

未执行 POSIX/Web 构建、远端矩阵或交互式 GUI 人工验收。内部 C++ 消费者需要同源码重编译。
