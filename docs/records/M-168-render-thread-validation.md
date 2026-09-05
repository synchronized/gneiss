<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-168：0.25.0 本地验收

## 结果

0.25.0 已完成 Windows Clang Shared/Static 本地构建与全量自动化验收。Editor、Runtime 和 Engine
复用同一渲染线程实现，Lantern Gallery、真实 Granit 窗口、安装消费、帧包所有权、队列回执、命令
进度、窗口恢复与关闭路径均通过测试。

## 验证

- `windows-clang-debug`：全量 131/131 通过。
- `windows-clang-static-debug`：全量 129/129 通过。
- Shared/Static 均从 Granit 0.7.0 锁定提交重新构建。
- `git diff --check` 通过。

## 待完成的远端验收

- Linux Clang/GCC Shared/Static。
- Granit Runtime Shared/Static。
- Address/Undefined Behavior Sanitizer。

以上矩阵由手动 Actions 提供。远端全部通过后，更新本记录、Plan 和 Roadmap 为完成状态，再进入
合并与标签流程。
