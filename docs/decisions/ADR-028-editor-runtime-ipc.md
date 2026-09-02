<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-028：Editor 与 Runtime 使用版本化本机 IPC

- 状态：已接受
- 日期：2026-09-02

## 背景

ADR-024 已确定 Editor 通过独立进程运行 Runtime。当前命令行负责传递工程和会话路径，标准流与日志
文件负责诊断，停止信号文件负责请求退出。这套机制能够隔离作者状态和运行时崩溃，但不能可靠承载
暂停、恢复、运行状态、心跳和双向错误反馈。未来游戏 Network Service 也可能使用 libuv，但本机
Editor 控制协议与不可信远程网络具有不同的生命周期、安全和传输语义。

## 决策

- Editor 作为本机 IPC Server，Runtime 作为由 Editor 启动的 IPC Client；独立命令行启动 Runtime
  仍然受支持。
- 首版 Transport 使用仅绑定 `127.0.0.1` 的 TCP，端口由系统分配。Editor 为每次会话生成一次性随机
  令牌，并通过子进程参数传递端点和令牌。
- IPC 使用带魔数、协议版本、消息类型和负载长度的显式帧；首版负载使用有界 JSON，不传输 C++
  对象布局、指针或第三方类型。
- 每个进程拥有一个内部 `uv_runtime`、一条专用 I/O 线程和一个 `uv_loop_t`。libuv handle 只由 I/O
  线程操作；Engine、Scene、ECS、UI 和游戏模块状态只由主线程操作。
- 主线程与 I/O 线程通过有界命令队列和事件队列交换拥有所有权的数据；libuv 回调不得直接调用
  Engine 或 UI。
- libuv 作为私有实现依赖。Gneiss 普通接口不暴露 `uv_*` 类型，0.15.0 不建立公共 Network Service。
- IPC 首版承载握手、能力协商、运行状态、暂停、恢复、停止、心跳、结构化日志和错误事件。
- 退出码、日志文件、标准流和 `child_process` 强制终止继续作为启动早期、断线和无响应时的降级路径；
  停止信号文件在新协议完成故障验收前不移除。

## 影响

- Editor 可以区分进程、连接和游戏运行状态，并在 Runtime 崩溃后保持作者会话可用。
- Pause 只停止游戏逻辑更新，不停止平台事件、IPC、诊断或必要渲染。
- 所有发送队列、接收帧和单帧处理量都必须有界；控制消息不得被日志流量阻塞。
- `uv_loop_t` 的关闭必须等待全部 handle 的 `uv_close` 回调完成，随后才能退出并连接 I/O 线程。
- 未来 Network Service 可以复用内部异步 I/O 基础，但需要独立定义连接 RID、远程输入安全、TCP、
  UDP、DNS 和线程契约。
- 后续若改用命名管道或 Unix Domain Socket，只替换 Transport，不改变业务协议与主线程状态机。

## 替代方案

- 继续使用信号文件和标准流：实现简单，但无法形成可靠双向控制和状态同步。
- 在主线程每帧调用 `uv_run(..., UV_RUN_NOWAIT)`：减少线程数量，但 I/O 延迟与帧率、暂停和调试卡顿
  绑定，不适合作为未来网络底层。
- 现在建立完整 Network Service：可以统一部分底层代码，但会在缺少游戏联网场景时过早冻结公共 API。
- 一开始使用平台管道：本机边界更严格，但 Windows 命名管道与 Unix Domain Socket 增加首版跨平台
  验收复杂度。
