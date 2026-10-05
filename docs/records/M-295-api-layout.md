<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：C ABI 适配目录迁移

日期：2026-10-05。落实 ADR-054 中 Engine 的公共适配目录。

## 实施范围

将 `src/api/c/` 的 10 个实现文件与日志校验头迁至 `src/engine/api/`，同步 CMake、
内部 include、公共定义清单检查及其正反例。逐文件比较确认内容仅有 include 路径变化，
不改变描述转换、错误返回、异常隔离、状态所有权或线程策略。

C 适配层仍允许定义公共 C 入口，但不再从 Engine 的宿主依赖检查中整体排除。
新增正反例确认适配入口合法，而包含 Apps、Editor、Tooling 均被拒绝。
检查只覆盖直接包含，不能替代完整依赖图审计。

公共 `include/gneiss/`、符号清单、安装入口和库名保持不变。当前目录 Concept、ADR 与
DEV-045 同步更新；历史 Record 保留当时路径。

## 验证结果

- Windows Clang Debug 共享与静态配置全量构建通过，包含公共 C11/C++20 头独立编译。
- 共享完整 CTest 175/175 通过，183.32 秒。
- 静态 ABI 消费者、安装消费者、稳定性清单与边界正反例 8/8 通过，23.27 秒。
- 使用 llvm-readobj 检查 DLL，102 个公共 C 导出与稳定性清单完全一致。
- 11 个迁移文件与迁移前版本逐一比较，仅 include 路径改变；公共头无改动。
- 修改的 C++ 文件 clang-format 检查、355 个文档相对链接与 git diff --check 均通过。
- 未重跑静态完整 CTest，未运行远端 Actions 或 Linux/macOS 矩阵。

## 剩余工作

Editor 可复用实现与根目录 Apps 入口、assetc 入口及维护脚本归属仍待整理。
完整 C++ SDK 语义审计与最终跨平台发布矩阵尚未完成，本次迁移不代表 0.45 发布完成。
