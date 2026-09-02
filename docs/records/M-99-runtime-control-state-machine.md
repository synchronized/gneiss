<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-99 Runtime 控制状态机实施记录

## 结论

M-99 已将可选 Editor IPC 会话接入 Runtime 主循环。Runtime 主线程拥有协议状态并消费有界 Transport
事件，I/O 线程不访问 Application、场景或游戏模块。没有提供完整 IPC 参数组时，Runtime 继续沿用
独立命令行运行和停止文件降级路径。

## 当前行为

- IPC 参数由 `--ipc-address`、`--ipc-port` 和 `--ipc-token` 组成，必须全部提供或全部省略。
- Runtime 连接后发送 `hello`，要求协商出 `control` 与 `heartbeat` 能力；认证完成前不执行游戏模块
  更新。
- 认证成功后发送 `ready` 和 Running 状态。Pause 只停止固定更新与逐帧游戏逻辑，Application 平台
  事件、渲染、日志和 IPC 继续运行；Resume 恢复游戏逻辑更新。
- Stop 转入 Stopping 并请求 Application 正常退出；关闭阶段发送 Stopping 与
  `shutdown_complete`。
- Runtime 对 Ping 回送相同 nonce 的 Pong；任意有效已认证消息刷新心跳期限。
- 握手超时、心跳超时、Transport 错误或 Editor 断开都会转入 Failed，请求 Application 安全退出并
  写入结构化日志。
- 未知兼容消息可以跳过；方向不合法的已知消息返回协议错误，不直接修改游戏状态。
- 既有停止文件仍可请求正常退出，待 M-101 故障矩阵完成后再评估移除。

## 验证结果

| 验证 | 结果 |
| --- | --- |
| 真实 Transport 鉴权与必需能力协商 | 通过 |
| Running、Pause、Resume、Stop 状态迁移 | 通过 |
| Pause 时游戏更新门控 | 通过 |
| Ping/Pong nonce 往返 | 通过 |
| 握手超时与心跳超时 | 通过 |
| Editor 断开后 Failed 与安全退出请求 | 通过 |
| 无 IPC 的 Runtime Smoke 与停止文件协议 | 通过 |
| Windows Clang 专项重复 | 连续 100 次通过 |
| Windows MSVC Runtime 与专项测试 | 通过 |
| Windows Clang Debug 完整回归 | 100/100 通过 |

Linux、Sanitizer、进程级 Editor–Runtime 控制与最终关闭消息送达验证留在 M-100/M-101 完成。M-100
将让 Editor 创建 Server、生成会话令牌、传递端点，并把工具栏与真实 Runtime 状态绑定。
