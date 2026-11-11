<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-116：强类型通知与独立日志系统

## 状态

2026-11-11：Signal 最小适配已实现并通过 Windows Clang 定向测试，尚未替换生产接口。承接用户优先整理日志与事件的要求，
PLAN-115 渲染实现暂不开始；原有计划不标记完成。
决策见 [ADR-119](../decisions/ADR-119-events-and-logging.md)，核查见[记录](../records/2026-11-11-event-log-feasibility.md)。
以下业务接入及日志 API 为目标示意；已实现的 Signal 契约见[事件接口](../reference/events.md)。采用已有 C++20、result 和 PLAN-109 异常策略。

## 目标与非目标

事件用于业务通知；日志用于可过滤、可丢弃的诊断。两者独立，不通过全局事件总线发送日志。
保留 Ludens 频道式 Logger 的调用便利，复用 EnTT 信号能力和 spdlog 同步输出能力。
不建立事件继承树、字符串事件名、反射事件注册、通用消息中间件，也不替换文件/任务/Render 的专用请求协议。
不要求所有通知异步，不为每条日志返回 result，不引入第二个日志队列。

## 当前基线

- core/log/log_dispatcher 拥有有界队列和专用消费线程，复制消息后交付 Application 配置回调。
- Runtime 负责文本/IPC 输出和轮转，Editor Console 有界保留日志；结构化源数据不能退化成拼接文本再解析。
- 现有 message_view 包含 context、source、category、result；event_view 含 sequence、timestamp_ns、thread_id。
  迁移逐字段核查实际生产者/协议使用，不能假定所有字段已在 IPC 中传输。
- EnTT 为仓库已有 3.15.0；尚未引入 spdlog。Ludens 代码仅作为参考，不复制其第三方依赖或全局对象。

## 事件模型

namespace 建议为 gneiss::engine::events。Signal 为发布者所有，Sink 仅授予订阅权限。
初期只做 void 通知，不加入投票/返回值收集；需要返回值或确定执行顺序的关系采用直接调用。

```cpp
class document {
public:
  events::sink<void(const document_change&)> changed();
private:
  events::signal<void(const document_change&)> changed_;
};
// 示意：监听对象必须存活到断开，订阅保存在监听方。
subscription_ = document.changed().connect<&panel::refresh>(*this);
```

- 信号、连接、断开和发布均限所属线程；EnTT 本身不提供跨线程安全保证。
- 发布参数短期借用；连接不保活监听对象；监听方在对象析构前断开。
- connection 显式断开；scoped_connection 移动拥有订阅、析构断开，不用 bool 判断目标对象存活。
- 包装层需使 Signal 先销毁后的连接析构安全，使用弱控制状态；不得把 EnTT 的裸 signal 指针直接泄漏出去。
- 首版禁止同一 Signal 递归发布及发布期间连接/断开、销毁监听者；调试期可检测并给出契约错误。
  这种限制也适用于回调中销毁 scoped_connection；不能依靠析构异常返回 result。
- 不保证监听顺序；回调异常传播到执行边界，停止本轮后续调用，不回滚已执行监听者。
- 第一闭环使用成员函数/自由函数绑定。捕获 lambda 的拥有型 connect 需要稳定 callable 存储和销毁规则，
  不把借用 lambda 的 EnTT 适配冒充拥有型接口；后续仅在真实调用方需要时加入。
- 适配模板的编译依赖按内部接口清单隔离；不使用公开 using 别名假装第三方类型已隐藏。

作用域 dispatcher 属于后续按需能力，不作为首版完成前置条件；若实施，明确 enqueue 拥有事件，
dispatch 只处理本轮开始时已有事件，回调新增事件留到下一轮，且不承诺跨事件类型的顺序。
跨线程仍经已有任务/mailbox 交给目标线程，再发布 Signal；不持队列锁调用业务回调。
关闭时先停止生产者再销毁 mailbox/Signal；事件排队中不保存裸业务对象指针。

## 日志 API 与对象归属

namespace 建议为 gneiss::engine::logging，避免通用 level/event_view 与业务命名混淆。

```cpp
auto log = logging::get_logger("asset");
log.info("加载资源：{}", uri);
if (log.enabled(logging::level::debug))
  log.debug("资源详情：{}", expensive_description());
```

Logger 为可复制轻量句柄；channel 使用 string_name，正文拥有 UTF-8 字符串。
格式参数编译期检查，先过滤再格式化；普通函数参数仍提前求值，昂贵参数必须显式 enabled 检查。
源码位置使用单独验证过的包装或可选宏，确保记录调用点；不强迫所有调用方写宏。
普通日志返回 void；创建文件 Sink、初始化配置等可预期失败返回 result。
critical 只是级别，panic/assert/OOM 使用独立 fatal 应急通道。

进程入口显式创建 logging runtime，早于 Application、晚于业务生产者关闭。
Application、工具、Render 和 Asset 只持 Logger，不需借 Application 写日志。
允许默认上下文，同时提供显式上下文用于测试和多宿主隔离；默认路由只在初始化/停止后修改。
缓存 Logger 不因默认上下文替换而悄悄改绑；失效上下文采用明确的关闭后降级策略，不保活工作线程。

## 原生记录、后端与输出

当前选择：Gneiss 原生记录及唯一分发队列 → 多 Sink → spdlog 同步终端/文件适配。
标准 spdlog log_msg 不含自定义 context/result/sequence，直接使用其异步队列无法原样携带这些字段。
不通过 payload 编码元数据再解码，不使用 TLS 向异步 Sink 传递上下文，不维护按序号查找的全局旁路表。

- 原生记录保留生产者时间、线程、频道、正文，以及确有使用的错误/上下文字段。
- 记录序号在统一接收点产生；过滤/丢弃可造成间隔，不作为业务事件 ID。
- string_view 只用于同步入口或 Sink 调用期间；异步队列拥有正文及动态来源文本。
- 原生 Sink 收到完整记录；spdlog 适配用该记录生成文本，不能把消费线程 ID 冒充生产线程 ID。
- Console/IPC Sink 放宿主层，编码原生记录；UI 从有界缓冲读取，不由日志线程操作面板。
- Sink 配置在启动时固定，级别过滤可安全调整；运行时任意增删 Sink 不在首版范围内。
- 不持队列或配置锁调用 Sink；Sink 禁止重入普通日志，错误报告走独立应急输出。
- spdlog 仅用于同步输出，不启用 async_logger/thread_pool；旧 dispatcher 逐步成为唯一 runtime 的内部实现，
  完成迁移后删除旧 Application 专属装配，不能长期同时维护两个队列。

## 背压、关闭与异常

桌面默认有界队列单消费线程，队满丢弃新普通日志并累计报告，不阻塞实时线程。
应急 critical/panic 输出与普通队列分离；普通日志不是可靠审计系统，不能承诺所有严重日志都落盘。
无线程平台采用同一记录和 Sink 语义的同步模式，浏览器仅使用适合平台的输出，不同步执行桌面磁盘路径。
队列容量、丢弃计数和每次刷新策略显式配置，不逐条 std::endl。

关闭：停止生产者 → 拒绝新记录 → 排空已接受记录 → flush sinks → 结束线程/关闭文件。
禁止在 Sink 回调内 flush 或关闭自身；在线生产者未停止时 flush 不等同于关闭。
正常文件 I/O 错误通过独立错误通道报告；bad_alloc/意外异常按 PLAN-109 进入 fatal 边界。
spdlog 的捕获/错误处理行为须核查，不能把 OOM 转成普通写失败后继续运行。
不要简单关闭第三方异常处理宏并改变整个进程行为；必要时直接适配同步 sink，在本地边界区分异常。
崩溃路径不等待日志线程、不尝试完整析构，不承诺所有日志完整落盘。

## 依赖及目录

```text
engine/core/events/
  signal.hpp
  connection.hpp
  detail/...
engine/core/log/
  logger.hpp
  log_types.hpp
  logging_runtime.hpp/.cpp
  sink.hpp
  detail/...
engine/platform/log/
  ...终端和文件 Sink 实现...
host/common/
  ...IPC 日志 Sink 与协议...
host/editor/
  ...Console 缓冲与视图...
```

Core 不依赖平台：Sink 通过接口注入，spdlog 头与编译宏限于具体适配实现。
EnTT 复用仓库版本与 MIT 许可证；spdlog 核查基线为 v1.15.3（MIT），不是宣称最新版本。
正式引入前按项目依赖流程锁定版本、校验值、许可证、std::format/fmt 配置及 Windows/Linux/Emscripten 构建。
优先 C++20 std::format，避免额外暴露 fmt；若工具链不支持，单独记录替代，不传播全局宏。
本轮不直接复制 Ludens 实现、不修改第三方仓库。

## 实施顺序

- [x] 阅读当前日志链与 Ludens，核查现有 EnTT，运行最小连接探针，记录 spdlog 数据模型差异。
- [x] 固定 Signal 适配契约并实现小闭环：成员绑定、RAII 断开、源先销毁、线程/重入限制。
- [ ] 核查实际订阅点，迁移一个对象级通知，不将任务完成、文件回调或渲染命令机械替换。
- [ ] 引入经锁定的 spdlog，同步 Sink 验证上下文、过滤、文件失败、格式错误及 PLAN-109 行为。
- [ ] 实现 Logger/原生记录/runtime；复用并收敛已有唯一队列，提供同步模式与内存 Sink。
- [ ] 接通 Runtime 文件/IPC、Editor Console、Application 创建前日志及关闭，逐字段核对现有协议。
- [ ] 迁移真实业务调用、删除旧入口与重复装配，同步 Reference/Concept、依赖清单及构建边界。
- [ ] 定向验收并记录浏览器、其他平台及尚未运行项目；不将源码审查写成平台验证通过。

## 验收与待核查

遵循 PLAN-114，复用既有日志协议、Console 和关闭用例，不为每个转发函数新增测试。
Signal 检查单线程生命周期、参数借用、断开后不再交付、源先销毁的安全断开与异常传播。
不执行会触发未定义行为的原生 EnTT 悬空连接/发布中容器修改来“验证安全”。
日志检查记录字段跨异步完整性、过滤、并发投递、队满统计、Sink 失败、重入与关闭、隔离上下文。
本地优先编译、小型 CPU 验证；不运行大场景、GPU、远端 CI，也不触发崩溃弹窗测试。

尚未验证：spdlog 适配实际编译、浏览器工具链、源位置捕获。源先销毁包装已通过 CPU 定向测试。
这些是后续实施门槛，不需要重新确认总体方向；若发现必须改变所有权或异常策略，再列出取舍。
