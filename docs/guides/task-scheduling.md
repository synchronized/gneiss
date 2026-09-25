<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 宿主任务调度与异步纹理加载

## 接口范围

此处描述 `src/core/tasks` 与 `application_asset_reload_internal.h` 的内部 C++ 接口，不属于安装 SDK
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
// 创建 Application 后注入；Application 必须先于 scheduler 销毁。
auto result = gneiss::application_internal::attach_task_executor(app, scheduler);
// 在帧循环不持有业务锁的位置显式驱动，再消费各服务完成结果。
auto driven = scheduler.run_ready({.max_tasks = 8U,
                                  .max_time = std::chrono::milliseconds(2)});
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

## 渲染资产请求与事务

内部宿主用 `request_render_assets` 提交 Mesh/Material/Texture 的 URI/类型批次，保存请求号，
通过 `poll_textures`（沿用原内部命名）驱动同一服务并消费一次终态；`request_textures` 是纯纹理
兼容入口。完成对象的 `assets` 保留全部依赖租约，`textures` 保留纹理租约，均须先于 Application
销毁。`reload=false` 且请求全部命中缓存时不读取磁盘。接口不属于公共安装 SDK。

CPU 准备复制只读 VFS 挂载表，在宿主执行器读取 JSON/二进制 Mesh、Material、PNG/KTX2/运行纹理，
将材质发现的纹理依赖加入闭包，同 URI 只准备一次。挂载后端在服务存活期间不得重配，
`read_bounded` 须支持并发只读；不支持有界读取的后端返回 unsupported。结束前逐一复查源字节，
不承诺跨进程原子文件快照。CPU 任务不访问缓存、RID 或 GPU。

主线程暂存 RID 和资源版本，渲染执行器分批创建 GPU 候选，全部确认后才整批发布。
已有 RID 保持不变，材质依赖随版本保留，旧帧持有旧数据；缺失资源只在成功后进入缓存。
失败回收候选并保留旧画面。请求接受时保存全局缓存修订，任何缓存映射变化或另一事务发布都会
保守地使该请求过期，包括不相关 URI；宿主可以在终态失败后重试。

`query_asset_load_progress` 仅查询所属线程状态；`cancel_render_assets` 在首次 GPU 命令接受前
返回成功并请求取消，之后返回 NOT_READY，当前批次继续完成或回滚。取消不是同步完成；仍须消费
终态。关闭先停止接收并收尾，再销毁 Application 和宿主池；解码及驱动调用不可强制抢占。

## 限额与可观察性

- 同一服务只允许一个在途请求或一个未消费终态，繁忙返回 NOT_READY。
- 通用请求及依赖闭包最多 256 个资产；独立源文件合计和 CPU 候选分别限制 256 MiB。
  单纹理准备仍限制 64 MiB；兼容纹理入口最多 16 个请求 URI。临时读取、解码、复查、GPU 暂存
  和旧帧资源额外占用内存，以上不是整个进程内存上限。
- 主线程每次最多暂存四个资产，资产之间检查 2 ms；渲染上传命令最多四个资源、预计 8 MiB。
  网格按实际打包顶点和索引估算，纹理按负载估算；单资源超过预算时单独执行，不无限饥饿。
  预算不能抢占单个资源或保证驱动耗时上限。网格独立缓冲避免每次更新重建全部几何。
- Runtime 保序保留最多 16 个待回复命令，只有最终成功才推进已应用修订；Scene/Prefab 结构更新
  和初始场景加载仍同步。Editor 待发队列保留原有 1024 引用/128 KiB URI 预算；服务闭包超限
  明确失败，不偷偷拆开原子事务。
- Asset IPC 域版本 3，Envelope 仍为 v2；旧域版本不宣称支持新行为。准备/上传以独立 progress
  事件每 100 ms 至多一次报告会话、修订、上传数量和可取消性。cancel 事件携带会话与修订，
  只匹配当前请求；过期或迟到取消不影响后续批次。原请求最终只回复一次 applied/failed/cancelled。
  Editor 忽略不匹配修订的通知，取消后可重新同步。心跳独立推进。
- 完成记录包含 CPU 准备、任务排队、提交等待（包含 GPU 等待）、GPU 命令耗时与候选字节数。
  协作模式单个准备任务仍会阻塞宿主，2 ms 预算仅在任务之间生效。

## 实际图形验证

`gneiss.asset-pixel` 从真实场景管线离屏输出回读 RGBA 像素，验证两模式纹理颜色、材质新依赖、
网格覆盖范围、失败保持旧画面与不同输出尺寸。它不是系统截图或性能采样接口。

`gneiss_texture_load_window_benchmark` 配合 `tools/performance/measure_texture_loading.py` 测量
大纹理，配合 `tools/performance/measure_model_loading.py` 重新导入 Lantern 并测量混合资产加载。
Windows 同时移动相机、连续 resize、最小化恢复，并独立验证加载期间退出；CSV 记录实际已呈现
帧完成间隔。单独报告导入工具耗时，不混入 Runtime 加载阶段；不以主循环 tick 代替呈现帧。
