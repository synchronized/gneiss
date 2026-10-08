<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# CPU 性能采集

使用可选 Tracy 配置记录主循环与后台任务的连续时间线。普通构建默认不下载或链接 Tracy。
采集会改变 CPU、内存与代码布局，不能代替[构建指南](building.md)中的性能验收。
依赖和模块边界见 [ADR-060](../decisions/ADR-060-optional-cpu-profiler.md)。

## 构建

```powershell
cmake --preset windows-clang-profiling -DGNEISS_ENABLE_GRANIT_PLATFORM=ON
cmake --build build/windows-clang-profiling --target gneiss_main_loop_response gneiss_profiling_capture
```

该配置使用 RelWithDebInfo，保留优化和符号。任意原生配置可设置
`-DGNEISS_ENABLE_PROFILING=ON`；Web 暂不支持。Windows 本地验证记录见 M-321，其他平台
尚需验证。离线构建可用 `FETCHCONTENT_SOURCE_DIR_GNEISS_TRACY_SOURCE` 指定固定版本源码。
运行目录应保留 `gneiss_tracy` 共享库，静态引擎的 profiling 配置也需要它。
公共头不包含 Tracy，安装消费者不需要自行添加其头路径或编译宏。

## 采集与查看

下载 [Tracy v0.13.1 工具](https://github.com/wolfpld/tracy/releases/tag/v0.13.1)，与 client
保持版本一致。打开分析器连接 `127.0.0.1`；或先在一个终端启动命令行采集：

```powershell
tracy-capture.exe -a 127.0.0.1 -o capture.tracy -s 600
```

再在另一个终端运行夹具或实际宿主，以下两条命令择一执行：

```powershell
# 128 次主循环与后台任务，最多等待分析器连接 20 秒。
build/windows-clang-profiling/bin/gneiss_profiling_capture.exe --wait-for-profiler

# 完整资产加载，无需打开旧的逐段 CSV 观测。
build/windows-clang-profiling/bin/gneiss_main_loop_response.exe response.json --assets <Cook资产根>
```

同一时刻只运行一个 profiling 进程和一个采集客户端。在分析器中打开 `capture.tracy`，
按主循环标记选择区间，查看主线程与工作线程。命令行可重新读取文件验证事件：

```powershell
tracy-csvexport.exe -u capture.tracy > zones.csv
```

正式应用不等待分析器连接，正常退出也不以分析器确认为条件；连接前、断连后及退出
尾部可能没有记录。需要完整启动阶段时使用专门夹具，不修改正式应用退出契约。
按需连接只监听本机、关闭广播，不将 profiler 端口作为产品远程接口。

## 事件口径

| 事件 | 含义 |
| --- | --- |
| `application.loop` / 帧标记 | 一次主循环，不是 GPU 呈现完成时间 |
| `application.events` / `application.update` | 输入与平台事件处理 / 宿主更新回调 |
| `application.render.submit` | CPU 渲染准备与提交，不是 GPU 执行耗时 |
| `application.idle_wait` | 主循环主动等待进展 |
| `application.idle_lock` / `application.idle_condition` | 等待前首次取锁 / 条件变量等待（包含唤醒后重新取锁） |
| `scene.advance` / `scene.verify.submit` / `scene.verify.step` | 场景推进 / 复验提交 / 复验任务体 |
| `scene.build.step` / `asset.advance` | 节点构建步骤 / 资产与上传推进 |
| `scene.builder.create` / `scene.assets.reset` | 复验完成后创建构建器 / 切换资产服务，包含辅助函数入口 |
| `scene.description.move` / `scene.instance.allocate` / `scene.instance.initialize` | 构建器内部的描述移动 / 实例分配 / 实例初始化 |
| `asset.prepare.step` / `asset.prepare.destroy` | 后台资产准备任务体 / 完成后销毁准备状态 |
| `asset.prepare.select` / `asset.prepare.prefetch` / `asset.prepare.open` | 选取资产 / 描述预读 / 建立读取及分配缓冲 |
| `asset.prepare.read` / `asset.prepare.read_slice` | 分块读取与验证 / 消费读取请求（含发起、等待或同步回退） |
| `asset.prepare.copy` / `asset.prepare.hash` | 保留输入副本 / 分块摘要更新 |
| `asset.prepare.decode` / `asset.prepare.verify` | 解析或解码并汇入批次 / 来源复验 |
| `asset.prepare.publish` / `asset.prepare.cleanup` | 排序并移交准备结果 / 清理准备状态 |
| `asset.verify.open` / `asset.verify.read_next` / `asset.verify.hash` | 来源复验的打开 / 分块读取与验证 / 摘要更新 |
| `asset.io.open` / `asset.io.begin_read` / `asset.io.reopen` | Windows 原生文件打开 / 创建异步读取 / 首次重开异步句柄 |
| `asset.fs.open_read` / `asset.fs.canonical` / `asset.fs.file_type` / `asset.fs.relative` | 原生 VFS 打开完整入口 / 规范化路径 / 检查文件类型 / 计算相对路径 |
| `asset.fs.path_build` / `asset.fs.canonical_call` | UTF-8 路径转换与拼接 / 单独的标准库 canonical 调用 |
| `asset.io.request.allocate` / `asset.io.request.start` / `asset.io.request.destroy` | Windows 异步请求缓冲分配 / 发起读取 / 析构函数体（可能包含取消等待，不含成员自动析构） |
| `task.submit` / `task.execute` | 调度器提交调用 / 执行及执行后收尾 |
| `task.complete` / `task.receive` | 发布终态 / 宿主消费回执 |

任务区域文本带有 `scheduler=<owner> task=<id>` 和任务名，数值为 task id。
任务名仅采集前 1024 字节，空名称不产生文本事件，不改变调度器保存的名称。
原生 VFS 打开与规范化外层区域附带资产相对路径文本，最多 1024 字节；空文本不记录。
导出前应留意路径是否包含项目内部名称。文本仅在采集区域有效时求值，关闭 profiling 时不求值。
查找相同二元组即可关联不同线程，不能仅凭任务名或单独 task id 区分调度器。
失败、取消或依赖失败可能没有执行区域，但仍有终态和被消费的回执；提交拒绝没有有效
task id。当前未绘制自动跨线程连线，也未接入 GPU、全量分配或互斥锁专用事件。

区域记录墙钟，等待和抢占可能包含其中；采样火焰图与区域时间线口径不同。系统级调度、
等待调用栈及缺页定位仍依赖平台能力和权限，接入 Tracy 不保证这些信息自动可用。
RenderDoc 用于独立检查渲染帧状态，不能替代这条 CPU 时间线。
