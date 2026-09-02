<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-101：示例与跨平台阶段验收记录

## 当前结论

M-101 的示例行为和 Windows 本地验收已完成，跨平台远端验收尚未执行，因此该里程碑保持“实现中”。
Lantern Gallery 的游戏模块会周期性发送 `runtime_progress` 结构化事件，测试由此观察真实游戏更新，
不以 Editor 按钮状态代替运行效果。

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

首次完整回归中 IPC Transport 曾出现一次无诊断的快速失败；该测试随后连续 20 次通过，第二次完整
101 项回归通过。当前没有证据表明它由本次示例改动引入，但远端矩阵仍需继续观察该时序风险。

## 待验收项

- Linux Clang/GCC 的 Shared 与 Static 矩阵。
- 远端环境下的 libuv handle、I/O 线程退出和重复会话稳定性。
- 必要的人工 Play、Pause、Resume、Stop 体验检查。

上述项目需要推送分支并手动触发 Actions；在获得用户明确授权前不执行，也不将其记录为已通过。
