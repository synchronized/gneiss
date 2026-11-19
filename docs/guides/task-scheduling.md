<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 宿主任务调度与异步纹理加载

## 接口范围

此处描述 `engine/include/gneiss/engine/core/tasks` 与 `function/application/application.hpp` 的内部 C++ 接口，不属于安装 SDK
或公共 C ABI。调度器安排执行，Service 管理请求、资源身份与事务；渲染仍由专用渲染执行器拥有。
设计取舍见 [ADR-047](../decisions/ADR-047-cooperative-task-execution.md) 和
[ADR-048](../decisions/ADR-048-runtime-asset-preparation.md)。

## 创建与驱动

Editor 和 Runtime 各自在宿主中创建一个 `task_scheduler`。默认使用三个工作线程；通过
`--cooperative-tasks` 选择由所属宿主线程执行任务。Editor 的导入、作者资产服务及 Application
借用同一个实例。Application 的公共同步创建接口不会自行创建额外任务池。

```cpp
gneiss::tasks::task_scheduler scheduler({
    .mode = gneiss::tasks::execution_mode::cooperative});
gneiss::runtime::application_configuration config;
config.executor = &scheduler;
auto created = gneiss::runtime::application::create(config);
if (!created)
  return created.error();
auto application = std::move(*created); // 必须先于 scheduler 关闭、销毁。
application->add_system(gneiss::app::stage::completions, "tasks", [&] {
  (void)scheduler.run_ready({.max_tasks = 8U,
                            .max_time = std::chrono::milliseconds(2)});
});
// 注册业务与停止系统，再调用 run；停止排空也会执行 completions。
return application->run();
```

`submit` 只接受和排队；`poll`、`query` 不执行任务。`run_ready` 只属于协作模式，错误模式、错误线程
及嵌套驱动返回独立状态。零任务数或零时间预算不执行任务。时间预算只在任务之间检查，无法抢占
一次 PNG 解码或导入；协作模式适配无线程宿主，不保证大任务期间界面持续响应。

任务容量包括尚未消费的终态回执。作用域取消关闭该作用域的后续提交，不影响其他服务。
`request_stop` 请求停止，`stopped` 查询任务是否全部终结，`try_close_scope` 取消并尝试非阻塞回收。
桌面 `close_scope` 和析构仍可等待运行中的任务；不得从自身任务销毁调度器或同步等待自身作用域。
无线程构建只允许宿主线程访问；启用线程后，提交、查询和取消可并发，驱动和最终销毁仍属于宿主。

最小 Emscripten 宿主、异常配置和真实浏览器测试命令见[构建指南](building.md#最小无线程浏览器调度验证)。
这里只验证调度模块，完整 Runtime/Editor Web 及 Emscripten pthreads 不在已验证范围内。

## 资产与场景加载

资产请求、缓存、批次取消与资源更新规则以 [资产参考](../reference/assets.md) 为准。
Application 负责服务完成分发，业务系统持有自己的请求；查询请求不驱动底层任务。
宿主配置、请求观察与关闭顺序见 [Application 参考](../reference/application.md)。

场景结构加载及视觉依赖由 Runtime/Editor 宿主协调。结构就绪与视觉资源完成是不同事件，
不要把加载状态转换等同于 GPU 已完成。示例 `examples/scene_viewer/main.cpp` 展示 loading/viewing
状态及双视图释放；Runtime 保持结构激活与后续视觉加载分离。场景实例所有权与切换规则见
[场景实例参考](../reference/scene-instance.md)。

## 实际图形验证

`gneiss.asset-pixel` 从真实场景管线离屏输出回读 RGBA 像素，验证两模式纹理颜色、材质新依赖、
网格覆盖范围、失败保持旧画面与不同输出尺寸；还验证场景切换前共享资源可更新、
旧 Scene 句柄在激活后失效，以及取消不撤回已成功更新。它不是系统截图或性能采样接口。

`gneiss_texture_load_window_benchmark` 配合 `scripts/performance/measure_texture_loading.py` 测量
大纹理，配合 `scripts/performance/measure_model_loading.py` 重新导入 Lantern 并测量混合资产加载。
Windows 同时移动相机、连续 resize、最小化恢复，并独立验证加载期间退出；CSV 记录实际已呈现
帧完成间隔。单独报告导入工具耗时，不混入 Runtime 加载阶段；不以主循环 tick 代替呈现帧。
