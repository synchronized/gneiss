<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Application 生命周期与主循环

## 原生入口与所有权

仓内宿主使用 `src/runtime/application.hpp` 的 `gneiss::runtime::application`，
配置为 `application_configuration`。`application::create(config, output)` 创建已初始化对象，
通过 `std::shared_ptr` 返回；失败保留原 output，不创建 Application RID 或注册表项。
完整 Application C API 与公共创建描述已删除。动态游戏模块也已撤销。

Application 拥有 World、平台状态、资源服务及可选窗口。`native_world()` 返回借用对象，
调用方不得销毁 Application 拥有的 World。关闭后借用 World 句柄失效。
最后一个 Application 引用必须在创建线程释放，析构执行关闭；也可显式调用 `shutdown`。
初始化失败会回收部分状态，已经进入初始化的平台回调与 shutdown 配对。

原生配置中的字符串在创建期间借用，回调由 Application 保存；回调捕获的借用对象必须
存活至关闭结束。不要让回调强捕获自身 Application，避免引用环。

## 主循环

`run(max_frame_count)` 每帧轮询事件、计算时间、调用 update 并提交渲染。
update 只接收帧时间，诊断回调只接收诊断事件，关闭请求回调不带参数；宿主状态由回调捕获。
`max_frame_count` 为零表示持续运行至退出，非零限制本次执行帧数。
多次调用 run 延续帧序号与累计时间；重入、跨线程或关闭后运行返回 `invalid_state`。

`request_exit()` 请求在回调返回后退出。`close_requested` 返回 false 可拦截平台关闭，
返回 true 或未提供回调时正常退出。暂停仍轮询事件和调用 update，但 delta_ns 为零。
回调返回失败或抛出异常会转换为原生错误结果，不允许异常绕过资源清理。

## 平台与窗口

`application_configuration::use_granit_window` 默认为 false，允许空回调无窗口模式。
窗口默认 1280×720。启用 Granit 窗口需要构建选项 `GNEISS_ENABLE_GRANIT_PLATFORM`；
未提供后端时请求窗口返回 `unsupported`。窗口模式不允许同时提供平台 initialize、
poll_events 或 shutdown 回调，业务 update 与时间回调仍可使用。

`get_window_size` 返回当前客户区尺寸。Win32 路径采用物理像素，与指针坐标一致；
无窗口模式返回配置尺寸。最小化时尺寸可能为零，恢复后再提交渲染。

日志回调在消费线程执行，其关闭与借用规则见[日志参考](logging.md)。
