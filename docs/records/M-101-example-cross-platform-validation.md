<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-101：示例与跨平台验收记录

## 结论

M-101 已完成。Lantern Gallery 的游戏模块会周期性发送 `runtime_progress` 结构化事件，测试由此观察
真实游戏更新，不以 Editor 按钮状态代替运行效果。Windows 本地矩阵和 Linux 手动 Actions 均已通过。

## 已验证行为

- Runtime 完成鉴权并进入 Running 后，Lantern Gallery 持续产生递增的运行进度事件。
- Editor 请求 Pause 并收到 Paused 后，排空传输中事件，观察窗口内进度保持不变。
- Editor 请求 Resume 后，进度继续增加，证明游戏更新真正恢复。
- IPC Stop 能正常关闭 Runtime 并得到零退出码。
- Editor 能在进程退出前收到 `shutdown_complete`，该断言随完整会话连续 10 次通过。
- 同一个 `runtime_process` 能再次启动 Runtime，并建立不同的 Console 会话。
- 既有进程测试继续覆盖无效工程启动诊断、构建失败、无响应子进程和强制终止。
- 安装树测试覆盖隔离安装前缀、已安装 Runtime、Lantern 模块重新配置与构建以及安装后启动。

## 本地矩阵

| 配置 | 验证内容 | 结果 |
| --- | --- | --- |
| Windows Clang Shared Debug | Lantern 端到端工作流连续 10 次 | 通过 |
| Windows MSVC Shared Debug | Lantern 端到端工作流 | 通过 |
| Windows Clang Static Debug | Runtime IPC 会话、Editor Runtime 进程与降级路径 | 通过 |
| Windows Clang Shared Debug | 完整 101 项测试 | 通过 |

Static 配置不构建动态游戏模块，因此不运行 Lantern 模块端到端用例；它验证静态 Engine 下的宿主、
IPC 与进程边界。游戏模块动态加载闭环由 Shared 配置负责。

## Linux 远端矩阵

[Linux Actions 运行 33591427595](https://github.com/synchronized/gneiss/actions/runs/33591427595)
基于提交 `2f7421a`，以下 7 项 Job 全部通过：

- GCC Core Shared 与 Static。
- Clang Core Shared 与 Static。
- Granit Runtime Shared 与 Static，包括无头窗口测试。
- Sanitizer Runtime，包括内存错误与退出泄漏检查。

首次远端运行 `33590968502` 的 Granit Runtime Static 暴露 IPC Transport 测试竞态：测试收到断线事件
后立即断言异步关闭回调尚未收敛的状态。修复后本地连续 200 次通过，完整 101 项回归通过，第二轮
Linux 矩阵全部通过；测试失败现在同时输出具体行号和表达式。

## 人工验收边界

本版本不追加新的人工 UI 验收。此前已完成 Editor 基本交互检查；当前 GUI 布局仍会在后续自研 GUI
方向调整。Play、Pause、Resume 和 Stop 的运行语义由真实进程自动测试覆盖，不以当前 ImGui 外观作为
0.15.0 的发布阻塞项。
