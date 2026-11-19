<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 日志消息契约

## 当前范围

日志类型统一位于 `gneiss::logging`，与 `events` 同步通知独立。
`core/logging/record.hpp` 定义 `level`、`message_view`、`record_view` 和迁移期 `record_callback`；
`logger.hpp`、`service.hpp/.cpp`、`sink.hpp` 分别提供频道句柄、上下文和输出契约。
`detail/async_queue.hpp/.cpp` 是内部异步队列，不单独提供 validation 模块。
输出工厂位于 `core/logging/sinks/file.hpp` 和 `sinks/console.hpp`；具体适配在
`engine/src/core/logging/sinks.cpp`，spdlog 只作为输出适配目标的私有依赖。
Application/Render 不再持有队列或接收队列指针，内部通过 Logger 提交。

Runtime 宿主安装默认 service，并以 Sink 接收 record_view；Application 不提供日志回调配置。
日志队列、基础 Sink、Runtime 传输协议与 Editor Console 保留；日志 C API 已撤销。

## 频道与日志服务

```cpp
namespace logging = gneiss::logging;
static const logging::logger log{"asset"}; // 持有频道名，不访问服务

logging::service logs; // 默认异步，唯一有界队列
// 文件 Sink 打开失败由调用者处理，此处省略。
auto connection = logs.attach(std::move(file_sink));
logging::default_service_binding binding{logs};
log.info("加载资源：{}", uri);
```

头文件分别为 `logger.hpp`、`service.hpp`、`default_service.hpp`。
默认 Logger 在调用时解析当前默认服务；未安装或关闭后禁用，不创建隐式线程。
默认路由槽保留至进程结束，仅持服务弱引用，避免静态析构顺序问题。
默认与显式 Logger 均持有 string_name；构造时保存频道，不借用调用者文本，可能分配。
`logger{channel}` 使用默认服务，`logs.get_logger(channel)` 固定绑定指定服务。
显式 Logger 固定绑定服务，关闭后不会改绑。默认路由只由宿主在停止生产者后安装/解除，重复安装触发断言。

`service_options` 支持 synchronous/asynchronous、queue_capacity 和 minimum_level。
同步模式不创建工作线程或队列，在调用线程串行输出；不要将磁盘 Sink 用于实时线程同步输出。
异步模式复用内部 async_queue。两种模式均不在配置锁下调用 Sink。
Logger 提供 trace/debug/info/warning/error/critical，以 std::format_string 检查格式。
`enabled` 同时检查服务阈值和是否存在可接收的 Sink，在格式化前过滤；昂贵参数仍须调用者显式判断。

`attach(unique_ptr<sink>, sink_options)` 返回非拥有连接；普通连接析构不解绑，
`scoped_sink_connection` 析构解绑。连接支持 connected、set_level、disconnect。
分发时快照 Sink 配置；断开阻止后续快照选中该 Sink，已开始的交付可完成，快照保活输出端。
输出端构造完成后再 attach，打开文件等可预期失败不交给服务处理。

宿主停止生产者、解除 binding 后调用 close：停止接收、排空、flush、释放 Sink。
flush/close 由宿主串行执行，禁止从 Sink 内调用；普通 Logger 重入被抑制。
`metrics().output_failures` 统计 Sink 返回的失败，分配及未知异常仍遵循执行边界策略。
Runtime 与 Editor 均在启动校验前安装默认服务；旧 Runtime 文件写入和轮转实现已移除。浏览器工具链尚未验证。

## 独立同步输出适配

`logging::sink` 位于 `engine/core/logging/sink.hpp`，同步接收完整 `record_view`，不保存借用字段。
`logging::create_file_sink(path, truncate)` 创建文件 Sink；输出适配目标为 `gneiss_logging_sinks`。
默认追加，可显式截断；宿主须在非实时线程串行调用 `write/flush`，正常关闭前显式 flush。
文件失败返回 `error::io`，分配及其他异常向执行边界传播。实现不使用 spdlog 的 logger 异常吞并路径。
文本包含原生单调时间、生产者线程、序号、上下文、级别、来源、分类、结果码和正文；不是 IPC 格式。
`create_rotating_file_sink` 提供按容量轮转，默认 1 MiB、一个备份，备份名遵循 spdlog（如 runtime.1.log）。
`sinks/console.hpp` 的 `create_console_sink()` 提供同步 stderr 输出，复用同一原生字段格式。
Runtime 使用轮转文件 Sink 与宿主协议终端 Sink；Editor 使用终端 Sink 与主线程交接缓冲。

## 字段与所有权

- `severity` 必须是 `TRACE` 到 `FATAL` 之一。
- `category` 是非空 UTF-8 筛选键；建议使用短小稳定的 ASCII 名称，例如 `game` 或 `asset`。
- `message` 是 UTF-8 正文，可以为空。
- `result` 可携带相关 Gneiss 结果码；没有相关操作时使用 `error::success`。
- 字符串使用 string_view 借用，不要求 NUL 结尾。校验和提交接口均不取得调用方内存所有权。

时间戳、线程标识和事件序号由投递器生成，来源由内部调用方填写。

## 提交与接收

Logger 可跨线程提交，异步模式在返回前复制文本，不等待输出完成。
每个 service 只有一个有界队列，队满丢弃新记录并累计报告。
服务由宿主管理，Application 不启动日志线程；未安装默认服务时默认 Logger 禁用。
Runtime 在创建 Application 前安装默认服务，将记录交给文件/标准输出 Sink 和可选 IPC Sink。
IPC 关闭前断开日志连接并等待日志队列排空，接收对象保持存活到在途交付结束。
正常退出应先停止生产者，再解除默认路由、关闭服务，释放 Sink。

Runtime 宿主 Sink 将记录接入标准流和单文件轮转输出。日志文件保持人类可读文本，
标准流使用 `@gneiss-log-v2 ` 前缀加单行 JSON。协议字段包含版本、单调时间、序号、级别、来源、
分类、线程、context、结果码和消息；字符串按 JSON 规则转义。启动失败也提交相同记录，
警告/错误写 stderr，其他级别写 stdout；IPC 和管道后备共用此编码，Console 按会话/序号去重。
协议版本 2 补齐 context，消费端与生产端须一起更新，不保留旧格式兼容分支。普通输出、未知协议版本和格式错误的协议行应由消费方保留为
Raw 输出，不能静默丢弃。

共享行解码器允许消费方按任意字节块输入。单行默认最多保留 64 KiB；超限行会标记为截断并丢弃
剩余字节，但不会影响下一行。子进程退出后，消费方必须调用 `finish`，以交付最后一个没有换行符的
尾部。该限制作用于跨进程传输，不改变进程内日志消息契约。

Editor Console 将协议事件与 Raw 行按到达顺序保存在最多 4096 条的有界记录中，并显示累计移除数量。
每次 Runtime 启动使用新的会话 ID。面板可以按严重级别、当前会话、来源、分类和文本组合筛选，支持
暂停可见截止位置、清空、复制当前结果及自动滚动。暂停只冻结显示，后台仍继续接收有界日志。

Application 的初始化或运行失败直接写入日志，保留失败来源、分类和错误码；具名系统使用系统名作为
来源。配置预检查失败直接返回结果，由宿主决定是否记录。Application 不再提供独立诊断回调或 report。
Render 在关闭时排空工作，再输出最终队列/GPU 统计与后端资源检查结果；不依赖 Application 转发。
宿主应让日志服务覆盖 Application 和 Render 的关闭过程。

## 校验与结构化提交

频道在 Logger 构造或 get_logger 时检查一次；内部正文约定为合法 UTF-8，不逐条扫描。
格式检查不保证字符串内容 UTF-8 合法性，外部 IPC 数据仍由协议解码入口检查。
独立 validation 模块及 application::submit_log 已删除。

```cpp
static const gneiss::logging::logger log{"assets"};
log.write({
    .context = 123,
    .severity = gneiss::logging::level::warning,
    .source = "loader",
    .category = "loading",
    .message = "资源尚未就绪",
    .result = gneiss::error::not_ready,
});
```

结构化 write 保留 context/result/source/category；空来源和类别使用 Logger 频道。
文本仅借用至 write 返回；异步模式复制完整记录。该接口不返回业务错误，普通日志用 info/error 等格式化入口即可。

## Editor 本地日志与对象通知

Editor 宿主持有 Console，Runtime 子进程适配器借用该模型；独立使用适配器时才创建私有 Console。
本地记录保留 session_id=0，Runtime 会话使用非零 ID，当前会话筛选不混入 Editor 日志。
`console_log_buffer` 是至多 1024 条记录的 UI 交接缓冲，无线程；满时淘汰最旧记录并累计提示。
日志线程只复制拥有型记录，主线程 drain 解锁后再写入 Console，不从 Sink 操作 ImGui。

Console 的 changed() 返回对象级 events::sink<void()>；追加、清空、切换会话后通知。
面板通过 scoped_connection 标记待自动滚动，避免每帧强制滚到底部。监听者先断开再析构；
通知中不得修改同一模型。它是 UI 模型变更通知，不是日志分发总线。

正常关闭先停止应用/后台生产者，再排空日志与 UI 缓冲。提前退出时 service 析构仍排空已接收日志，
Sink 只持共享缓冲，不引用已销毁的 Editor 状态。性能采样的 stdout 行保留为工具验收输出。
本版不新增源文件/行号捕获包装；source 表示逻辑来源，不冒充调用点信息。


## 实现边界

`service.cpp` 管理配置、输出端及关闭；`logger.cpp` 负责过滤与提交；
`sink_connection.cpp` 管理连接；`default_service.cpp` 唯一维护默认路由。
`detail/service_state.hpp` 共享内部控制块与输出保护，`detail/async_queue` 唯一维护异步队列。
控制块用于弱引用失效检测与在途提交，不增加公开状态对象。

重复默认绑定、绑定或 attach 已关闭服务、在输出回调内 flush/close，以及内部队列意外拒绝
属于契约违例，使用断言。配置值非法仍沿用构造/配置入口的参数异常；文件打开、输出失败
继续通过错误结果处理。队列满仍丢弃并汇总诊断，不改成致命错误。
