<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-168：0.25.0 跨平台验收

## 结果

0.25.0 已完成 Windows 本地构建与全量自动化验收，并在最终代码提交上通过 Linux 和 Windows
手动 Actions。Editor、Runtime 和 Engine 复用同一渲染线程实现，Lantern Gallery、真实 Granit
窗口、安装消费、帧包所有权、队列回执、命令进度、窗口恢复与关闭路径均通过测试。

## 验证

- `windows-clang-debug`：全量 131/131 通过。
- `windows-clang-static-debug`：全量 129/129 通过。
- Shared/Static 均从 Granit 0.7.0 锁定提交重新构建。
- [Linux Actions](https://github.com/synchronized/gneiss/actions/runs/33961601450)：Clang/GCC
  Shared/Static、Granit Runtime Shared/Static 和 Sanitizer 共 7 个任务全部通过。
- [Windows Actions](https://github.com/synchronized/gneiss/actions/runs/33961603176)：MSVC Runtime
  Shared/Static 和安装后 C/C++ Consumer Shared/Static 共 4 个任务全部通过。
- `git diff --check` 通过。
