<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# M-137 IPC 耦合审计与模块化决策记录

## 结果

现有 IPC 已达到模块化触发条件。协议包含 17 个全局消息类型，并承载 Session/Control、Log、
Inspection、Statistics 和 Property 五类职责；属性写入之外，场景检查也具有双向快照与重同步语义。

审计确认保持 Transport 与线程模型、直接升级内部协议 v2，并以协议域和注册式 Dispatcher 替换双方
集中分发。决策见 [ADR-034](../decisions/ADR-034-modular-ipc-protocol.md)。

## 主要耦合

- `ipc_protocol.h` 同时保存全局消息枚举、握手、控制、日志和通用错误数据对象。
- Runtime 会话先特判 Property 帧，再把其他消息解码为通用对象并进入 Control `switch`。
- Editor 会话分别特判 Inspection、Statistics、Property 结果，再用条件链处理状态、错误、日志和关闭。
- 专用协议虽然已经拆分源文件，仍依赖全局消息编号，无法独立声明方向、能力和请求关联。
- 当前 16 字节帧只有全局消息类型与通用 flags，没有协议域和通用请求 ID。

## 保持不变的边界

- Editor Server、Runtime Client 与一次性会话令牌。
- libuv 专用 I/O 线程及主线程强类型命令/事件队列。
- 回环 TCP Transport、进程退出码、日志文件和强制终止降级路径。
- 有界帧、有界队列、单帧消费预算及 Control 优先级。
- JSON 作为首版域内负载，第三方类型不进入普通接口。

## 后续验证重点

- 协议域拆分不能改变暂停、恢复、停止、心跳或连续 Play 的状态机顺序。
- Property 使用通用请求 ID 后仍需保持期望修订、超时、晚到响应和断线清理语义。
- Inspection 分片与重同步必须继续保持会话 ID 和严格序号检查。
- 未注册域、错误方向或低优先级流量过载不能拖垮 Control 域。
