<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-98 Editor–Runtime 协议实施记录

## 结论

M-98 已完成内部 Editor–Runtime 协议契约。协议在 M-97 帧之上使用有界 JSON，覆盖握手、运行状态、
控制、结构化日志承载、错误、心跳和关闭完成消息；不访问 Engine、Editor UI 或游戏模块状态，具体
运行状态迁移留给 M-99。

## 消息与兼容策略

- 首版消息为 `hello`、`hello_ack`、`ready`、`state_changed`、`pause`、`resume`、`stop`、
  `log_event`、`error`、`ping`、`pong` 和 `shutdown_complete`。
- 帧头是消息类型与协议版本的权威来源，JSON 不重复保存这些字段。
- 当前协议版本为 `1.0`。主版本不同返回 `unsupported`；同主版本的未来次版本可以按已知字段读取，
  未知消息类型返回 `unsupported`，由上层安全跳过。
- JSON 负载上限为 64 KiB；令牌、能力数量、能力名称和文本字段均有独立上限。
- `hello` 携带一次性会话令牌与 Client 能力集合。Server 使用与输入长度无关的比较过程校验令牌，
  `hello_ack` 返回双方能力交集，顺序保持 Client 请求顺序并去重。
- 同主版本协商双方支持的最低次版本；Client 拒绝 Server 宣告的未来次版本或未请求能力。
- `ipc_timeout_tracker` 使用调用方提供的单调时钟时间点，可分别用于握手和心跳超时，不引入隐藏线程
  或墙上时钟依赖。

## 验证结果

| 验证 | 结果 |
| --- | --- |
| 12 种消息 JSON 往返 | 通过 |
| 所有 Runtime 状态值往返 | 通过 |
| 正确令牌、错误令牌与主版本不兼容 | 通过 |
| 次版本协商、能力交集与重复能力去除 | 通过 |
| 未知消息、字段类型错误、非对象 JSON 与超限文本 | 通过 |
| 握手与心跳边界超时 | 通过 |
| 真实回环 Transport 上的 hello/hello_ack | 通过 |
| Windows Clang 专项重复 | 连续 100 次通过 |
| Windows MSVC 专项构建与测试 | 通过 |
| Windows Clang Debug 完整回归 | 99/99 通过 |

Linux、Sanitizer 与最终 Shared/Static 矩阵在 M-101 统一执行。M-99 将消费这些消息并建立 Runtime
控制状态机、心跳响应和 Editor 失联退出行为。
