<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Core、平台适配与 ABI 辅助命名统一

日期：2026-10-21。依据 ADR-069 与模块布局计划。

## 交付

基础工具归 engine::core，任务、日志、反射、诊断、数学、输入共享值使用其领域子空间。
帧时间值为 engine::core::frame_time，TRS 为 engine::core::math::transform。
输入值从功能层命名移至 core::input，服务仍为 engine::input::input_service。
窗口、I/O、CAORS 适配归 engine::platform 及 io/caors 子空间，第三方 CAORS 使用 ::caors。
C 值转换辅助归 engine::api，应用日志回调桥归 runtime::api。
全部调用方同步更新，不提供旧命名空间别名。

本批未改变对象所有权、调度、缓存、I/O 行为或公共 C ABI；内部 C++ 使用者须同源码重编译。
边界检查增加旧命名负向夹具，规则只匹配命名声明与引用，避免把文件名误判为命名空间。

## 验证

- Windows Clang Debug 完整构建通过。
- 61 项定向回归全部通过，包含任务、libuv、CAORS、日志、反射、输入、场景、编辑和边界检查。
- 无线程静态 CAORS Application 构建和测试通过。
- 共享/静态安装包的 8 项 consumer 重链接与运行通过。
- clang-format 和 git diff --check 通过。

本地日志位于 build/core-platform-namespace-*.log。未运行图形窗口、Linux/macOS/Emscripten 或远端 Actions。

## 剩余范围

进程、IPC、动态库对象仍位于 gneiss 顶层，需下一批处理类型查找和平台构建；
本批不代表全部命名或整个架构版本已完成。
