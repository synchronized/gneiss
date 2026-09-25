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

## 纹理请求与事务

内部宿主调用 `request_textures`，保存返回的请求号，通过 `poll_textures` 驱动提交并取一次终态。
返回成功只代表请求已接受；完成对象的 `result` 和 `state` 才代表最终应用结果。完成对象中的纹理租约
必须先于 Application 销毁。`reload=false` 且整批已缓存时直接复用租约，不读取或解码磁盘数据。

服务复制 VFS 挂载表并持有后端共享所有权。挂载后端在服务存活期间不得重配，有界读取须支持只读
并发访问。原生文件后端在分配前检查大小；不支持有界读取的自定义后端明确返回 unsupported。
准备完成前重新检查描述和源字节，发现变化则失败；不自动无限重试，也不保证跨进程原子文件快照。

CPU 任务只处理读取、PNG/KTX2/运行纹理封装验证和自有候选数据。主线程检查缓存身份和 RID 版本，
暂存候选后将上传提交给专用渲染线程。Granit 同步批量上传完成回执确认 GPU 复制完成，随后才发布。
首次加载失败会释放暂存 RID；重载失败保留旧资源。已加载纹理保持 RID，切换不可变数据版本，
材质引用继续有效，已提交帧保留旧快照。同步混合资源事务替换缓存后，迟到候选会被拒绝并回收。

提交许可前可取消；上传已被接受后，当前批次完成或回滚再产生终态。关闭时先停止接收，再收尾服务，
最后销毁宿主池。Runtime IPC 断连按现有协议退出进程，未提交请求取消；不迁移到新连接继续回复。

## 限额与可观察性

- 一个纹理服务同时保留一个请求及一个未消费终态；繁忙时返回 `NOT_READY`。
- 一个请求最多 16 个纹理，输入文件上限 64 MiB，整批 CPU 候选上限 64 MiB。读取、解码、复查及 GPU
  暂存会产生额外临时内存，因此候选限额不是整个进程内存上限。
- 主线程每次最多暂存四个纹理，任务之间检查 2 ms；渲染执行器一次上传一个批次，单次上传不能抢占。
- Runtime 保序保留最多 16 个待回复命令，保留原始请求 ID；只有最终成功才推进已应用修订。
  Editor 保持单个在途批次，待发送队列最多 1024 个资产引用、128 KiB URI 字节；纯纹理批次不合并到
  16 个以上，超额请求明确拒绝。已知资产的完整重同步超过限额时需要重启 Runtime。
  心跳独立推进，资产完成不使用属性编辑的两秒超时。
- 完成记录包含 CPU 准备、任务排队、提交等待、GPU 命令耗时与候选字节数；失败不提前发送 applied。
  混合 Texture/Material/Mesh 批次仍整体同步执行，初始场景加载也仍同步。

真实大纹理测量工具为 `gneiss_texture_load_window_benchmark` 与
`tools/performance/measure_texture_loading.py`。它生成 4096×4096 RGBA PNG，并在同步、线程池和
协作模式下重复运行，Windows 同时改变相机、连续 resize、最小化恢复。CSV 记录已呈现帧的完成间隔，
被队列替换或因尺寸过期跳过的帧不计为已呈现帧。窗口最小化和 GPU 上传仍可能造成较长呈现间隔，
不能将主线程不阻塞等同于固定帧率。
