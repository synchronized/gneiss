<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-295：Function 六模块目录迁移

日期：2026-10-05。落实 ADR-054 的功能层目录。

## 实施范围

Application、World、Scene、Render、Input、Game 的 74 个文件迁至 `src/engine/function/`
下同名模块。内部 `.h` 改为 `.hpp`；Granit 渲染后端随 Render 进入其 `backend/granit/`。
公共 `include/gneiss/`、二进制名称和安装入口保持原有路径。

CMake、内部 include、宿主和测试引用同步更新；不增加兼容头或重复实现。
逐文件与迁移前版本比较，排除 include 和保护宏后，74 个文件的内容完全一致。
本轮不修改状态所有权、运行算法、上传事务、场景行为或线程策略。

边界检查同时识别旧路径和新 Function 路径。新增反例验证 Render 不能包含 World、Scene、
Application，Platform 不能包含 Function，Engine 不能包含 Apps、Editor 或 Tooling。
Asset 与 Core 原有禁止反向包含规则继续有效。检查为直接包含检查，不能代替完整依赖图审计。

## 剩余工作

C ABI 适配仍在 `src/api/c/`；Editor 可复用实现与 Apps 入口、assetc 入口及维护脚本的迁移
尚未完成。宿主配置语义和完整 C++ SDK 审计仍按 DEV-045 推进，目录迁移不代表整版发布完成。
历史 Record 与版本归档保留当时的路径。

## 验证结果

- Windows Clang Debug 共享、静态配置均完成全量构建。
- 共享完整 CTest 175/175 通过，184.06 秒；静态完整 CTest 172/172 通过，103.12 秒。
- 新旧路径的边界正反例检查通过；自有源码无旧 Function include 路径残留。
- 修改文档的 350 个相对链接校验通过；公共 `include/gneiss/` 无改动。
- 提交前 `git diff --check` 通过。未运行远端 Actions、Linux/macOS 或外部大型场景验证。
