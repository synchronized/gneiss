<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-199～M-205：0.31.0 游戏工程工作流验收记录

## 结果

Gneiss 0.31.0 已贯通游戏工程的创建、首次配置、构建、运行与目录包导出。Editor 与
`gneiss_project` 命令行工具共享 `templates/game` 脚手架和事务式工程操作；新工程采用场景格式 v2，
具有独立模块标识并显式声明配置预设。

Runtime 启动前会使用工程声明的预设完成配置与构建。目录包导出只收集运行所需的工程描述、资产、
游戏模块、Runtime、动态库和平台启动脚本，不携带源码、构建目录、CMake 工程文件或完整 SDK。
工程创建和导出均拒绝覆盖既有目标，失败不会留下可被误认为成功结果的半成品。

版本号已提升至 0.31.0，未改变公共 C ABI、资产二进制格式或 Editor–Runtime IPC 协议。

## 本地验证

- Windows MSVC Shared Debug：完整构建及 132/132 测试通过。
- Windows Clang Shared Debug：完整构建通过，开发警告继续按错误处理。
- 安装 SDK 冒烟覆盖：创建外部工程、配置、构建、Runtime 启动、目录包导出及包内启动通过。
- `git diff --check` 通过。

## 远端验证

- [Linux Actions 34029995414](https://github.com/synchronized/gneiss/actions/runs/34029995414)：
  Clang/GCC Shared/Static、Granit Runtime Shared/Static 和 Sanitizer 共 7 个作业全部通过。
- [Windows Actions 34029997169](https://github.com/synchronized/gneiss/actions/runs/34029997169)：
  MSVC Runtime Shared/Static 与安装 Consumer Shared/Static 共 4 个作业全部通过。

完整远端矩阵针对候选代码仅执行一次；验收记录与状态更新不改变构建、依赖或测试结果，因此不重复
触发 Actions。
