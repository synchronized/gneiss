<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-120：平台 IPC 与反射协议消息

## 目标与已确认决策

先整理平台 IPC，再迁移协议负载到反射 JSON。固定 24 字节帧头不变，不增加二进制后端。
平台只拥有单连接 TCP、增量帧解码和有界队列；协议层拥有域编号、角色、握手、方向及消息规则。
分类依据本地协议配置，不能信任对端声明的优先级。保持日志可丢弃、控制消息优先的现有策略。
协议重构使用同版本 Editor/Runtime 验收；改变负载格式时更新域版本，不隐式伪装兼容。

## 阶段一：平台与协议边界

- [x] 平台类型迁入 gneiss::ipc，文件名去除 ipc_ 前缀。
- [x] 平台信封只存域编号；业务域与 dispatcher 迁到 protocol。
- [x] 发送和接收统一使用本地分类器，移除平台日志硬编码。
- [x] poll_events 改为 drain_events，保持完成队列语义。
- [x] 验证帧格式、分片、拒绝、背压、断连、关闭与实际宿主通信。

## 阶段二：类型化消息

- [x] dispatcher 拥有规则，删除借用 registry 的包装及 Router 动态分配。
- [x] Editor/Runtime 通过 initialize_meta(protocol::register_types) 在冻结前注册协议类型。
- [x] 运行统计负载迁入反射 JSON，保留字段名称及 session/sequence 业务校验。
- [x] 提供类型化负载入口，限制字节大小、保留诊断、返回独立候选；验证真实宿主链路。
- [ ] 注册类型化消息描述：域、操作、kind、方向与处理器；字段由 meta 注册。
- [x] 贯通控制请求执行及反射状态通知返回 Editor 的实际链路。
- [ ] 逐域迁移其余负载，删除手写字段编解码和 JSON 片段桥接。
- [ ] 反射解码独立候选后调用处理器，业务合法性仍由业务层检查。

## 生命周期与失败

宿主创建 transport、配置本地分类规则，启动 server/client；完成事件由所属线程 drain 后交给
协议路由，I/O 线程不调用场景或编辑器逻辑。停止生产消息后 stop，释放传输对象。
本地分类器是无状态 noexcept 函数，不借用会话，发送和接收可并发调用。
无效字节、未知消息、队满、断连继续返回错误或完成事件；不改为断言。

## 验收

保留平台 framing 与生命周期测试，协议校验测试归 protocol。真实消费者为 Editor 与 Runtime。
新增普通消息只定义结构、注册字段、绑定处理器，不改平台。平台阶段不得声称反射迁移已完成。

## 非目标

不新增网络服务、多连接、远程认证、IPC 二进制反射后端或兼容旧负载的第二条路径。


## 平台阶段验收

Windows Clang Debug 共享构建通过，16 项协议、IPC、Runtime 会话和边界测试通过。
静态配置平台帧解码与传输构建及 2 项测试通过；该配置未启用 protocol 测试目标。
未执行 Linux/Web 或远端验证。

## 统计消息阶段验收

统计消息已完成反射迁移；其余域及类型化消息处理器仍按阶段二继续推进。
Windows Clang Debug 共享构建通过，11 项协议和宿主测试通过，包括 Runtime → Editor
统计接收、重启与关闭。无效 JSON、缺失字段、大小限制及业务拒绝不覆盖输出已验证。
端到端测试的两处旧 stage 日志匹配已更新为现有结构化 category 字段。

## Control 消息阶段

源码核查确认暂停、恢复、停止请求与 ready 通知没有负载，操作由帧头表达；不为统一形式
添加空 JSON 对象或复制操作码。实际有数据的 state_changed 由单字节改为反射 JSON，
Control 域升级到 v2，大小上限 1 KiB。同版本宿主配对使用，不增加 v1 兼容分支。
状态枚举和消息字段在协议注册入口注册；反射生成独立候选，合法状态检查后才更新输出。
业务状态冲突仍由 Runtime 处理，沿用已有协议错误反馈与状态通知，不新增响应机制。
类型化处理器描述的整体收敛及其余域迁移仍未完成。

共享构建及 17 项 IPC、协议、宿主测试通过，覆盖非法字段、大小限制、旧版本拒绝、重复暂停
冲突，以及真实进程暂停、恢复、停止和重启。未执行静态、Linux/Web 或远端验证。

## Scene 消息阶段

- [x] Scene 加载/取消请求直接反射 ipc_scene_request，删除借用 Asset 请求包装的路径。
- [x] 进度与可选预算结构通过反射编解码，phase 使用注册枚举名；删除手写 JSON 字段处理。
- [x] 保留 URI、会话身份、进度上限、终态取消限制和清理互斥规则，候选通过检查后才替换输出。

Scene 域升级到 v2：请求直接保存 session/revision/uri；进度把身份放在 source 对象中，
其余字段遵循内存结构，可选 budget 使用通用 optional 规则。不保留 v1 格式兼容。
嵌套消息类型提供静态 GUID，使字段访问器与注册记录的身份一致；字段注册仍归协议模块。

Windows Clang Debug 共享构建通过，17 项 IPC/协议/宿主测试通过，包括实际 Runtime 场景加载。
协议测试覆盖各阶段、预算有无、数值边界、清理冲突及失败不覆盖输出。
未执行静态、Linux/Web 或远端验证；其他协议域和类型化处理器描述仍未完成。

## Scene 类型化路由试点

先以 Scene 验证调用简化，不继续机械迁移其余负载。消息描述统一负载类型、域、操作、kind、
发送方向与数据校验；域版本和预算保持域级配置。发送/解码和方向规则复用同一描述。
Runtime 类型化处理器仅接收请求身份及已校验对象；场景命令队列、状态冲突和队满处理不变。
Router 将上下文作为调用参数传递，不经 pending_context 临时状态中转。
Scene 同一 load 操作的 request/response 必须可区分；终态响应和清理事件不混用。
试点不改变线格式。遗留域继续使用原解码器，后续以实际迁移移除旧入口。

### 试点验收

- [x] Scene 四种消息描述驱动发送、独立候选解码与域方向规则。
- [x] Runtime Scene 处理器只接收类型化请求和 request ID，保留命令队列及背压。
- [x] 实际 Editor/Runtime 收发迁移，删除 Scene 四个旧编解码入口。
- [x] 操作路由去除 pending_context 中转，按 domain/operation/kind 区分绑定。

共享构建及 17 项相关测试通过，包含真实场景加载、请求/响应同操作绑定、重复绑定拒绝、
错误方向/负载不入队和队满。未执行静态、Linux/Web 或远端验证。
本次不改线格式；域协议规则表与处理器表仍分离，其他域使用旧入口，整体类型化消息注册任务
仍未标记完成。下一步按实际收益评估迁移，不以所有负载都改成反射作为完成标准。

## Asset 类型化迁移

复用 Scene 消息描述，迁移 reload/resync 请求、cancel 事件、两种结果响应及 progress 事件。
取消使用仅含 session_id/revision 的描述；Runtime 保留现有队列表示，不另建权威状态。
逐项结果直接反射嵌套 asset，operation 经 getter/setter 转为整数码，域版本升为 v5。
保留请求 1024 项、结果 256 项、URI 去重和资源分组规则，以及各类 8 条待处理命令的背压。
保留部分成功语义；进度仅 preparing/uploading，can_cancel 仅在 preparing 允许。
移除手写 JSON、旧编解码入口及仅转发的宿主发送包装；不扩展通用消息框架。

### Asset 验收

- [x] 六种消息描述、反射字段注册及实际 Runtime 类型化处理器接入。
- [x] 删除手写 JSON、旧编解码入口及两个仅转发的宿主发送源文件。
- [x] 保留部分成功、分组/去重、批次限制、取消和背压语义。
- [x] Windows Clang Debug 共享构建及 19 项相关测试通过，包含 Editor 资产重导入实际工作流。

资产工作流测试补齐冻结前协议注册，与宿主入口一致。未执行静态、Linux/Web 和远端验证。
Property/Inspection/Session/Log 尚未完成类型化迁移，PLAN-120 整体仍在推进。

## 发送接口复核与 Property 后续范围

- [x] Scene 加载/取消改为具名入口，删除发送层 bool 选择，保持请求 ID 与取消事件语义。
- [x] 复核 Asset operation 的批次来源与结果关联，保留实际动态选择，不添加转发对象。
- [x] 将消息、域规则、Router、Session 的当前职责记录到
  [IPC 参考](../reference/platform-ipc.md#消息与业务入口的职责)。
- [x] Windows Clang Debug 共享构建及 19 项相关测试通过；未执行静态或跨平台验证。

Property 接入评估：value 已复用反射，外层字段仍手写 JSON，并有负载和 envelope 两套入口。
下一步应迁移完整的 write/result 收发链，移除这两层重复包装。保留对象 generation、
expected_revision 冲突检查、请求关联及成功结果的 canonical_value；这些属于业务规则，
不能用通用反射替代。失败结果不要求规范值，需先明确消息字段策略再调整域版本。
本轮仅完成评估，尚未迁移 Property，不扩展通用框架来模拟属性编辑业务。

## Property 收发链迁移验收

- [x] 注册写入与结果字段，复用 value 反射，移除手写 JSON。
- [x] 以 property_write/property_result 驱动编解码、域规则和 Runtime 类型化绑定。
- [x] Editor/Runtime 实际发送和接收迁移，删除八个旧 codec 入口及两个转发源文件。
- [x] 请求 ID 只保留在帧头；保留队满回复、停止命令、修订冲突和规范值业务语义。
- [x] Windows Clang Debug 共享构建及 20 项相关测试通过，含真实会话属性往返、队列洪泛
  后停止和 Runtime 属性编辑器；未执行静态、Linux/Web 或远端验证。

Property 域升为 v2，失败结果也携带 value；GUID 文本遵循通用解析规则。格式及职责以
[IPC 参考](../reference/platform-ipc.md#property-类型化消息)为准。此前 Property 接入评估已落实。
Inspection/Session/Log 仍未完成类型化迁移，整体计划保持进行中。

## Log IPC 收发链迁移验收

- [x] 注册 runtime_log_record 字段，使用 log_record 类型化描述及派生域规则。
- [x] Runtime 直接编码记录，Editor 直接消费独立记录，移除 IPC 内文本前缀和二次解析。
- [x] 删除原 Log 信封编解码入口，保留日志丢弃分类、16 KiB 上限和控制台去重。
- [x] Windows Clang Debug 共享构建及 19 项相关测试通过，覆盖日志字段往返、非法负载、
  大小/版本限制、真实会话日志接收、Runtime 启停及 stdout 行协议。未做静态或跨平台验证。

Log IPC 域升为 v2；记录版本与 stdout 前缀仍为 v2。当前两条日志通道的职责见
[IPC 参考](../reference/platform-ipc.md#log-类型化记录)。发送适配器保留，因为它负责将借用的
logging::record_view 转为拥有文本的协议记录，并非同类型转发。

剩余：Inspection、Session 类型化迁移；stdout 手写文本编解码是否可安全复用反射，需要先
核查 Meta 初始化前及关闭后的日志调用，不以统一编码为由引入启动或关闭依赖。
整体阶段二仍未完成。

## Inspection 字段迁移

- [x] 注册对象身份、stamp、node、change、batch，批次递归使用反射序列化。
- [x] 删除手写 GUID/变换/组件字段编解码及最后一个 reflected_json.hpp 桥接文件。
- [x] inspection_snapshot 描述驱动快照方向及信封解码，保留独立候选和业务校验。
- [x] 格式升为 v3，采用嵌套 stamp/local_transform 与通用 GUID null；remove 也携带 node。

这轮保留现有分片算法、批次与分片包装入口，未声称 Inspection API 已全部收敛。后续收尾需
明确分片失败与非法负载的错误区别，再删除冗余发送包装；Session 和 stdout 编码评估仍待处理。
格式事实见 [IPC 参考](../reference/platform-ipc.md#inspection-批次反射)。

验收：Windows Clang Debug 共享构建及 19 项相关测试通过，包含大批次分片、重复 ID 拒绝、
自定义组件往返及失败不替换完整快照、真实 Runtime/Editor 工作流。Property 测试初始化已与
宿主 initialize_meta 流程一致。未执行静态、Linux/Web 或远端验证。

## Inspection 分片发送收敛

- [x] 完整逻辑批次经 encode_inspection_snapshot 返回带诊断的信封列表。
- [x] 整批校验重复 ID；区分序列化失败与大小超限，不再将非法内容误报为内存不足。
- [x] 探测时预留最大分片序号/数量的字段宽度，保持 64 KiB 与 128 个发送分片上限。
- [x] Runtime 直接取得帧列表，Editor 直接按消息描述解码；删除字节分片、v2 信封包装及
  Runtime 纯转发源文件。低层单片 payload 入口仍用于边界测试，resync 空负载接口不变。

本轮不改变 Inspection v3 格式；改变的是非法输入与分片容量失败的错误分类。完整结果发布后
才进入发送队列，现有网络背压与发送重试机制不变。Session 迁移及 stdout 编码评估仍待处理。

验收：Windows Clang Debug 共享构建及 19 项相关测试通过，包括完整批次分片/拒绝与真实
Runtime、Editor 工作流。未执行静态、Linux/Web 或远端验证。

## Session 字段迁移

- [x] 注册域能力、Hello、心跳、错误和关闭负载，删除手写 JSON 字段编解码。
- [x] 消息描述派生方向规则；Runtime 心跳与错误接收使用类型化绑定，删除心跳纯转发文件。
- [x] 保留认证、域协商、超时、独立候选输出和负载限制；补充类型化非法输入验证。

格式升为 Session v2，Hello 响应显式携带空 token；事实见
[IPC 参考](../reference/platform-ipc.md#session-类型化消息)。旧 Session codec 委托和部分宿主
包装仍保留，后续需沿实际调用链收敛。stdout 编码评估尚未处理，阶段二仍进行中。

验收：Windows Clang Debug 共享构建及 19 项相关测试通过，包含握手协商、错误令牌、
负载限制、心跳数值拒绝和真实 Runtime/Editor 工作流。未执行静态库、跨平台或远端验证。

## Session 收发入口收敛

- [x] Editor/Runtime 的 Hello、心跳、错误及关闭调用统一使用消息描述编解码。
- [x] 删除八个旧 codec 入口，以及四个纯转发源文件和 CMake 条目。
- [x] 测试直接使用正式接口，保留认证协商、超时、断连、控制生命周期和属性往返覆盖。

不改变 Session v2 格式。Runtime 保留响应请求关联及协商范围校验；Editor 保留认证和
发送成功后才进入已认证状态的顺序。会话语义没有下沉到通用协议层。当前接口见
[IPC 参考](../reference/platform-ipc.md#session-类型化消息)。

验收：Windows Clang Debug 共享构建及 19 项相关测试通过，git diff --check 通过。
未执行静态库、跨平台或远端验证。Session 上述包装已收敛；stdout 编码评估及整体计划
剩余验收仍待继续，不将本次迁移视为 PLAN-120 全部完成。
