<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 平台 IPC

`gneiss_ipc` 提供 `gneiss::ipc` 单连接回环 TCP 传输。入口为
`gneiss/engine/platform/ipc/transport.hpp` 与 `envelope.hpp`。
帧头保持 24 字节网络字节序，域为非零 uint16 编号；平台不解释业务域。

## 调用顺序

宿主创建 transport，server 调用 start_server 并取得 endpoint，client 调用 start_client。
连接完成后 send；所属线程调用 drain_events 消费完成队列。stop 停止并收尾，析构兜底关闭。
生命周期操作仍由调用方外部同步，不在 I/O 线程执行场景或编辑器操作。

构造第三参数 message_classifier 为可选无状态 noexcept 函数，返回 true 表示可丢弃消息。
未提供时所有消息均按普通消息处理。函数可在发送与接收线程调用，不得解析并修改业务对象。
可丢弃发送不占最后四分之一（至多 16 项）配额；接收满时普通消息优先替换可丢弃消息。
容量耗尽仍可能拒绝或丢弃，保留丢弃计数，不承诺无限可靠排队。

## 宿主协议

`gneiss_protocol` 提供 domains.hpp 中的业务编号和 is_discardable 策略；实际 Editor/Runtime
会话安装此策略以维持日志背压行为。分类来自本地，不接受对端优先级声明。
`protocol::dispatcher` 直接拥有域规则及处理器，负责握手、方向、版本和负载上限检查。
域处理器及上下文须活到分发结束；Router 不再单独维护注册表与动态分发器。

统计负载通过反射 JSON 编解码，字段名称保持不变。Editor/Runtime 入口先调用
`initialize_meta(protocol::register_types)`，再创建应用；普通编解码不触发注册。
`protocol/payload.hpp` 的 `encode_payload(value, limit)` 与 `decode_payload<T>(bytes, limit)`
返回带序列化诊断的 result。解码前检查大小并构造独立候选；业务层验证成功后才更新输出。
编码大小检查发生在 JSON 生成后，并非内存分配预算。统计域上限为 64 KiB，仍验证
session_id 和 sequence 非零。Control v2 的状态通知同样使用反射 JSON，例如 `{"state":4}` 表示 paused，状态字段必填，
域负载上限为 1 KiB。暂停、恢复、停止请求和 ready 通知仍使用空负载；请求操作与关联 ID
来自帧头，不在负载中重复。状态冲突使用既有协议错误反馈，状态改变通过通知返回 Editor。
Control v1 不兼容该格式，同版本宿主须配对使用。其余域尚未迁移。反射消息迁移进度见
[PLAN-120](../plans/PLAN-120-ipc-protocol-boundaries.md)。
