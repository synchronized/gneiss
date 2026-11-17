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
Control v1 不兼容该格式，同版本宿主须配对使用。Scene v2 的加载/取消请求直接保存 `session/revision/uri`，不再借用 Asset 请求；
进度保存嵌套 `source`、枚举名称 `phase`、进度字段与可选 `budget`。负载上限仍为 16 KiB。
终态响应、取消和清理补报语义不变。其余域尚未迁移。反射消息迁移进度见
[PLAN-120](../plans/PLAN-120-ipc-protocol-boundaries.md)。

## Scene 类型化调用

`protocol/message.hpp` 的消息描述统一负载类型、域、操作、kind、发送方、负载上限及数据校验。
Scene 提供 `scene_load`、`scene_cancel`、`scene_progress`、`scene_result`；域操作方向表由这些
描述生成。同一 load 操作的 request 和 response 使用不同描述，分别绑定处理器。

```cpp
auto envelope = protocol::encode(scene_load, request, request_id);
if (!envelope)
  return envelope.error().error;
return transport.send(*envelope);
```

Runtime 通过 `router.bind(scene_load, handler)` 注册处理器，签名为
`error(runtime_command_context&, protocol::request_context, ipc_scene_request)`。
路由先校验握手、协商版本和方向，再解码独立候选并验证数据，成功后才调用业务处理器。
处理器仅借用当前上下文；可将请求移动到现有命令队列，队满仍返回 not_ready。
绑定与分发须在所属线程串行执行；处理器捕获的借用对象须活到解绑或 Router 销毁。

操作路由直接传递 Context，不保存 pending_context；dispatcher 的 validate 只校验规则，
dispatch 保留原域回调方式供未迁移消费者使用。协议规则表与业务处理器表仍分离，
不是所有域都已完成类型化绑定。原 Scene 四个 encode/decode 包装已移除。
编解码入口保留序列化诊断；现有 dispatch_outcome 的处理失败仍只携带 error。

## Asset 类型化消息

Asset v5 使用 `asset_reload`、`asset_resync`、`asset_cancel`、`asset_reload_result`、
`asset_resync_result`、`asset_progress` 六个消息描述；方向表由描述生成。
Runtime 类型化处理器接收已校验的请求，不读取 envelope。每类命令的队列上限仍为 8 条。
请求上限 1024 项、结果上限 256 项，继续检查资源分组、重复 URI、进度范围和部分成功语义。
取消仅传 session_id/revision，大小上限 256 字节；其他消息上限 256 KiB。

结果 items 直接保存 `{asset: {uri, type}, operation, applied}`，operation 是有符号错误码。
枚举 type/status 按注册名称编码，error 通过字段 getter/setter 转换；不增加专用 JSON 编码器。
preparing/uploading 只允许作为进度事件，其他有效状态作为结果响应；can_cancel 仅允许 preparing。
取消不占用原请求响应，终态仍回到原 request_id。v4 与 v5 不兼容，同版本宿主需配套升级。
旧 Asset 编解码函数及宿主转发发送包装已移除。

## 消息与业务入口的职责

- 消息描述负责负载类型、帧身份、方向、大小上限及负载自身的合法性；字段编解码交给反射。
- 域规则负责版本协商和允许的操作，由消息描述生成操作规则，避免手写第二份方向定义。
- Router 负责校验与解码后调用处理器；处理器负责当前运行状态、排队与业务执行。
- Session 负责连接、请求 ID 和发送，不拥有场景或资产操作的执行状态。

Editor 的场景发送使用 `send_scene_load(request)` 和 `send_scene_cancel(request)`。
加载分配请求 ID，取消是事件，不分配新的响应身份。Runtime 场景通知仍根据终态及原请求 ID
选择响应或事件，以保留清理补报；调用者无需再次决定帧 kind。

资产发送保留批次的 reload/resync operation：它来自待处理批次，执行结果须回应同一种操作。
Runtime 资产通知另允许 progress 事件。这里的 operation 有业务来源和响应关联用途；不为了
消除参数增加逐层同义转发接口。Scene/Asset 共用消息编解码和路由机制，各自保留业务校验。
