<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-100：Editor Play 工作流实施记录

## 结论

Editor Play 工作流已接入真实 Editor–Runtime IPC 会话。Editor 先监听回环地址，再为每次运行生成
独立的 32 字节随机令牌并启动 Runtime；Runtime 完成鉴权握手后，双方通过版本化协议交换状态、控制、
心跳和结构化日志。无 IPC 或 IPC 故障时，现有标准流、日志文件、停止文件与强制终止路径继续有效。

## 已实现行为

- Editor 会话明确区分构建、连接、运行、暂停、停止和失败状态。
- 顶部控制栏、Run 菜单和快捷键绑定真实状态：F6 Play、F7 Pause/Resume、F8 Stop。
- Pause 与 Resume 通过 IPC 驱动 Runtime 游戏逻辑状态，不只改变界面按钮。
- Stop 优先发送 IPC 请求；超时后复用 `child_process` 强制终止，并可重新建立全新会话。
- Editor 定期发送 Ping，并对握手和心跳设置超时，避免失联进程长期悬挂。
- Runtime 将鉴权后的结构化日志发送到 IPC；标准流仍作为启动早期和故障场景的降级通道。
- Console 以会话、来源和序号识别重复结构化事件，避免 IPC 与标准流同时到达时重复显示。

## 验证

- Runtime IPC 会话测试覆盖鉴权、状态切换、结构化日志、Ping/Pong 与 Stop。
- Editor 进程测试使用真实 Runtime，覆盖握手、Running、Pause、Resume、Stop 和日志去重。
- 原有构建失败、无效工程、无响应子进程与强制终止测试保持通过。
- Windows Clang 的 Editor 进程测试连续重复 5 次通过。
- Windows MSVC 的 Runtime IPC 会话与 Editor 进程测试通过。

## 后续边界

Lantern Gallery 可观察运行逻辑、Linux 与完整构建矩阵、重复会话以及关闭消息交付的最终验收归入
M-101。本记录不声明远端 Actions、Linux 或安装树矩阵已经完成。
