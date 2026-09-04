<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-140：Session 与 Control 域协议实施记录

## 结果

已将会话和运行控制消息定义为独立的 v2 强类型域协议。现有双端会话在 M-142 统一组合前继续使用
v1 Transport 路径，保证特性分支的中间提交可构建、可运行；这不构成 v1 兼容承诺。

Session 域包含：

- Hello 请求与响应，以及“域 + 版本”能力协商；
- Heartbeat 请求与响应；
- 带原始结果码的协议错误；
- Runtime 关闭完成事件。

Control 域包含 Runtime Ready、运行状态事件，以及 Editor 发起的 Pause、Resume、Stop 请求。状态
负载使用一个固定宽度枚举字节，空控制请求不携带 JSON。

## 方向约束

同一域内操作可按两个发送方向分别声明允许的消息语义。例如 Hello 仅允许 Runtime→Editor 请求和
Editor→Runtime 响应；Heartbeat 仅允许 Editor→Runtime 请求和 Runtime→Editor 响应。该需求同时
细化了 M-139 Dispatcher 的方向规则，避免仅以“操作双向”放宽校验。

## 验证

- 验证 Hello 往返、常量时间令牌比较、请求顺序保留、能力交集与版本降级。
- 验证重复域、空请求 ID、错误令牌和非法负载拒绝。
- 验证心跳、协议错误、关闭通知和运行状态往返。
- 验证 Pause、Resume、Stop 请求以及 Session/Control 的双向消息语义规则。
- 继续运行原有 Runtime IPC 会话测试，约束现有状态机行为不发生变化。
