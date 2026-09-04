<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-143：IPC 兼容与故障验收记录

## 结果

0.21.0 IPC 模块化重构已完成 Windows Clang Shared/Static Debug 本地验收及 Linux 全矩阵。Editor 与
Runtime 的
运行、暂停、恢复、检查快照、属性写入、日志、停止、连续会话、握手与心跳超时、断线和低优先级
消息积压均由现有单元测试或端到端测试覆盖。

本次发现三项 CMake 测试直接匹配中文终端输出，在 Windows 当前代码页下会因转码而误报失败。
断言已改为校验结构化日志中的稳定 ASCII 字段、结果码和完整路径；用户可见的中文诊断保持不变。
Lantern Runtime 工作流还补充了启动失败时的状态与 Console 记录，便于后续定位偶发失败。

## 验证

- Windows Clang Shared Debug：120/120 项测试通过。
- Windows Clang Static Debug：118/118 项测试通过。
- Lantern Runtime 工作流在 Shared Debug 下连续运行 5 次，全部通过。
- Linux Actions：Clang/GCC Shared/Static、Granit Shared/Static 无头运行时和 Sanitizer 共 7 个任务
  全部通过，运行记录为 [33843740880](https://github.com/synchronized/gneiss/actions/runs/33843740880)。
- `git diff --check` 通过。

Static 构建期间 libuv 第三方源码仍产生既有的 Clang 弃用警告；这些警告不来自 Gneiss 自有目标，
且未影响构建或测试。
