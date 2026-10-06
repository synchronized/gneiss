<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Application 生命周期与主循环

## 生命周期

`gneiss_application_create` 初始化平台、可选 Render Service，并创建一个由 Application 独占拥有
的 World。初始化任何
阶段失败时，已经进入初始化流程的平台回调仍会收到一次 `shutdown`，随后返回原始错误。销毁顺序
固定为 World、平台回调状态、Render Service、平台窗口、Application；销毁后借用的 World 句柄
立即失效。

除[日志提交](logging.md)外，Application 及其 World 只能在创建线程访问。重复运行、跨线程调用、无效句柄和重复销毁均返回
明确错误。C++ 的 `gneiss::application` 提供不可复制、可移动的 RAII 包装。

`reset()` 返回关闭结果：成功或句柄已失效时清空所有权，其他失败保留句柄供重试。直接调用
`reset();` 的既有源码继续可用；依赖原 `void` 返回类型的成员函数指针需要更新。
`release()` 清空包装并返回 `released_application`，载体同时拥有句柄与回调适配存储；
其 get() 仅借用 C 句柄，reset()/析构负责关闭，`application::adopt` 可收回所有权。析构或移动覆盖若关闭失败则终止进程，
不会静默遗失所有权；需要处理错误时先显式 `reset()`。创建失败或无法关闭原输出时保留原包装。

## 主循环

`gneiss_application_run` 每帧按以下顺序执行：

1. 调用 `poll_events`，处理平台事件与关闭请求，并形成当帧输入快照。
2. 通过 `now_ns` 或内部单调时钟计算帧间隔。
3. 调用 `update`，传入帧序号、帧间隔、累计运行时间和暂停状态。
4. Granit 平台模式下提取 World 渲染快照、提交对象并呈现帧。
5. 响应回调中通过 `gneiss_application_request_exit` 发出的退出请求。

`max_frame_count` 非零时限制本次调用执行的帧数，适合测试和工具；零值表示持续运行至窗口关闭或
主动退出。多次调用 `run` 会延续 Application 的帧序号和累计运行时间。

暂停期间仍轮询事件并调用 `update`，但 `delta_ns` 为零且累计时间不增长。窗口关闭和主动退出属于
正常结果；事件或更新回调失败会立即终止本次运行并返回对应错误。

描述结构可通过 `close_requested` 拦截平台关闭请求。回调返回非零时主循环正常退出；返回零时继续
本帧，宿主可显示未保存确认，并在用户确认后调用 `gneiss_application_request_exit`。未设置回调时
保持直接关闭的默认行为。回调在 Application 创建线程同步执行，不得抛出异常或重入主循环。

## 平台适配边界

`gneiss_application_get_window_size` 查询当前窗口客户区尺寸，仅限创建线程。
Granit 原生 Win32 路径返回渲染客户区物理像素，与原生指针输入单位一致；不是额外除以 DPI 的
逻辑尺寸。Callback 模式返回创建描述中的窗口尺寸。最小化时 Granit 窗口可能返回零尺寸，调用者
应等待恢复，不应把它当作永久初始化失败。该查询不提供字体缩放或显示器 DPI 值。

`GNEISS_APPLICATION_PLATFORM_CALLBACK` 使用描述结构中的生命周期回调，也允许全部回调为空的
无窗口模式。生命周期回调在创建线程同步执行，不得重入 `run`，C++ 回调实现不得抛出异常；
日志回调在专用消费线程执行，其寿命与关闭规则见[日志参考](logging.md)。
`user_data` 由调用方持有，必须至少存活到 `shutdown` 返回。

构建时启用 `GNEISS_ENABLE_GRANIT_PLATFORM` 后，可以选择
`GNEISS_APPLICATION_PLATFORM_GRANIT`。Application 将按描述结构中的标题、尺寸和窗口标志创建
Granit Window，耗尽每帧事件队列，并把目标窗口的关闭事件转换为正常退出。Application 同时创建
Granit Renderer、Surface、Swapchain 与 Frame Context；当前每帧清屏，并在存在 primary Camera
和 Mesh Renderer 时绘制 Triangle List。窗口尺寸变化时重建 Swapchain，最小化产生零尺寸时暂停
提交。该模式不允许同时提供 `initialize`、
`poll_events` 或 `shutdown` 回调；`update`、`now_ns` 和 `user_data` 仍可使用。

未启用构建选项时请求 Granit 平台返回 `GNEISS_ERROR_UNSUPPORTED`。Granit 类型和句柄均不会进入
Gneiss 公共 ABI；运行时适配私有链接 `granit::granit`、`granit::window`、`granit::input` 与
`granit::render_pipeline`。

## 原生 C++ 配置与回调

`application::create` 接收独立 `application_desc`，配置字符串使用 string_view，平台和窗口标志使用
强类型枚举。窗口默认 1280×720，环境强度默认为 1；未知平台、窗口标志和超出 uint32 范围的
字符串长度在调用 C ABI 前拒绝。结构尺寸及保留字段不向普通 C++ 调用方暴露。

`desc.callbacks` 的函数表复制到稳定存储，描述对象可在 create 返回后销毁。userdata 仍由调用方
持有至关闭完成；回调函数必须 noexcept，在回调内自行处理异常。更新回调收到 application_ref 与
frame_time；诊断和日志回调收到原生值结构，文本只借用到回调返回。日志运行于消费线程，其余
回调在创建线程执行。application_ref 不拥有句柄，不提供销毁和释放操作。

移动、release 和 adopt 同时转移稳定回调存储；关闭先调用 C ABI 并等待日志排空，再释放存储。
关闭失败时保留上下文，不能提前销毁。载体不允许隐式转换为整数，C 互操作时必须保留载体直到
C 操作结束；直接用 C 函数销毁后，再 reset 载体会安全清空已失效句柄。

`create_native` 是显式 C ABI 互操作入口，接受 C 描述，原生 C 回调及 userdata 的寿命由调用方负责。
它不提供 C++ 回调表的复制与适配，不是 create 的旧签名兼容重载。
