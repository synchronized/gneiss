<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Application 生命周期与主循环

## 生命周期

`gneiss_application_create` 初始化平台、可选 Render Service，并创建一个由 Application 独占拥有
的 World。初始化任何
阶段失败时，已经进入初始化流程的平台回调仍会收到一次 `shutdown`，随后返回原始错误。销毁顺序
固定为 World、平台回调状态、Render Service、平台窗口、Application；销毁后借用的 World 句柄
立即失效。

Application 及其 World 只能在创建线程访问。重复运行、跨线程调用、无效句柄和重复销毁均返回
明确错误。C++ 的 `gneiss::application` 提供不可复制、可移动的 RAII 包装。

## 主循环

`gneiss_application_run` 每帧按以下顺序执行：

1. 调用 `poll_events`，处理平台事件与关闭请求。
2. 通过 `now_ns` 或内部单调时钟计算帧间隔。
3. 调用 `update`，传入帧序号、帧间隔、累计运行时间和暂停状态。
4. Granit 平台模式下提取 World 渲染快照、提交对象并呈现帧。
5. 响应回调中通过 `gneiss_application_request_exit` 发出的退出请求。

`max_frame_count` 非零时限制本次调用执行的帧数，适合测试和工具；零值表示持续运行至窗口关闭或
主动退出。多次调用 `run` 会延续 Application 的帧序号和累计运行时间。

暂停期间仍轮询事件并调用 `update`，但 `delta_ns` 为零且累计时间不增长。窗口关闭和主动退出属于
正常结果；事件或更新回调失败会立即终止本次运行并返回对应错误。

## 平台适配边界

`GNEISS_APPLICATION_PLATFORM_CALLBACK` 使用描述结构中的生命周期回调，也允许全部回调为空的
无窗口模式。所有回调均在创建线程同步执行，不得重入 `run`，C++ 回调实现不得抛出异常。
`user_data` 由调用方持有，必须至少存活到 `shutdown` 返回。

构建时启用 `GNEISS_ENABLE_GRANIT_PLATFORM` 后，可以选择
`GNEISS_APPLICATION_PLATFORM_GRANIT`。Application 将按描述结构中的标题、尺寸和窗口标志创建
Granit Window，耗尽每帧事件队列，并把目标窗口的关闭事件转换为正常退出。Application 同时创建
Granit Renderer、Surface、Swapchain 与 Frame Context；当前每帧清屏，并在存在 primary Camera
和 Mesh Renderer 时绘制 Triangle List。窗口尺寸变化时重建 Swapchain，最小化产生零尺寸时暂停
提交。该模式不允许同时提供 `initialize`、
`poll_events` 或 `shutdown` 回调；`update`、`now_ns` 和 `user_data` 仍可使用。

未启用构建选项时请求 Granit 平台返回 `GNEISS_ERROR_UNSUPPORTED`。Granit 类型和句柄均不会进入
Gneiss 公共 ABI；运行时适配私有链接 `granit::granit` 与 `granit::window`。
