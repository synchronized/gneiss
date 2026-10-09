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

Windows 连接采集时，`asset.fs.canonical_call` 文本额外记录 `os_tid`、`cpu_100ns`
和 `cycles`，分别是系统线程 ID、调用前后内核与用户态 CPU 时间之和的增量、线程执行
周期增量。计数读取失败记录 `thread_counters_unavailable`，不伪造零值。
CPU 计数单位为 100 ns，但实际记账较粗，零增量不代表完全没有执行；周期不能直接换算
为耗时。该区域包含计数查询与记录的观测开销，未连接或关闭 profiling 时不查询计数。
它能辅助区分持续计算与非执行时间，不能识别具体 I/O、缺页、锁或抢占原因。

## 独立文件元数据对照

Windows profiling 构建提供手动诊断夹具 `gneiss_filesystem_trace_probe`，只链接 Tracy，
不链接引擎、调度器或渲染器，不注册为自动测试。它遍历指定目录中的普通文件，不读取
payload、不修改资产，默认不跟随目录符号链接。先构建：

```powershell
cmake --build build/windows-clang-profiling --target gneiss_filesystem_trace_probe
```

在两个终端先后启动 collector 和夹具；采集期间不要并行构建或测试：

```powershell
build/tracy-tools/unpacked/tracy-capture.exe -a 127.0.0.1 -o filesystem.tracy -s 180
```

```powershell
build/windows-clang-profiling/bin/gneiss_filesystem_trace_probe.exe <Cook资产根> --wait-for-profiler
```

夹具最多等待连接 20 秒。每个文件执行标准库 canonical 和原生打开属性句柄、查询最终
路径、关闭句柄两组操作；相邻文件交替顺序，第二轮反转。查看 `probe.canonical`、
`probe.open_attributes`、`probe.final_path` 和 `probe.close_handle`，路径与顺序位于
区域文本。成功完成 N 个文件的两轮操作后，每类操作应有 2N 个事件。

连接模式退出前留出 200 ms 发送时间，仅适用于该诊断夹具，不保证尾部事件送达；必须
核对事件数，缺失时不能将已记录区间的最大值视作整轮最大值。原生对照没有实现标准库
的全部回退与路径前缀处理，不能直接作为 canonical 的生产替代。交替顺序也不能消除
全部缓存和系统状态差异；未复现不能证明此前的停顿已修复。

## Windows 系统事件联合采集

当长区间已经定位到文件系统或条件变量内部，使用 WPR 补充文件操作、硬缺页及线程
切换事件。此步骤需要管理员终端；Tracy 本身不因此获得系统事件权限。脚本当前已验证
预检查及模拟异常清理，真实管理员采集闭环仍待验证。

准备上述 profiling 构建和 Tracy v0.13.1 命令行工具后，在仓库根目录执行：

```powershell
# 只检查条件，不创建会话或输出目录；条件不足返回 2。
python -X utf8 -B scripts/performance/capture_windows_trace.py --assets <Cook资产根> --check

# 在管理员终端执行，默认最多等待夹具 900 秒。
python -X utf8 -B scripts/performance/capture_windows_trace.py --assets <Cook资产根>
```

脚本默认使用 `build/windows-clang-profiling/bin/gneiss_main_loop_response.exe` 和
`build/tracy-tools/unpacked/tracy-capture.exe`；可通过 `--exe`、`--tracy` 指定。
`--output` 必须是尚不存在的目录，默认创建 `build/windows-trace-日期-时间/`。
仅运行一个 profiling 宿主和一个 Tracy collector，采集期间不要并行构建或跑 GPU 测试。

WPR 使用 `GeneralProfile` 与 `FileIO` 的文件模式，独立实例名记录在 `run.json`；
命令语义见 [WPR 文档](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-command-line-options)。
文件模式可能产生较大的 ETL，存放在指定输出目录。退出或夹具失败后仍尝试保存：

- `system.etl`：用 WPA 打开，检查 File I/O、Disk I/O、Hard Faults、CPU Usage (Precise)。
- `application.tracy`：打开具体 canonical 或主循环区域。
- `response.json`：保留夹具通过/失败结果，采集成功不意味着性能达标。
- `run.json`、`fixture.log`、`tracy.log`、`wpr-start.log`、`wpr-stop.log`：PID、UTC 时间、
  二进制摘要、退出状态及日志。

先以 PID 与文件路径找到同一进程和资产，再检查慢操作的持续时间、线程切换和调用栈。
Tracy 相对时间与 ETL 起点不能直接相减；UTC 起止记录只用于定位进程生命周期。
符号缺失、事件丢失或未复现都应记录，不能据此断言不存在阻塞。

脚本不会自动取消其他采集。若 WPR 保存失败，保留现场，按 `run.json` 的
`stop_command`（参数数组）重试保存；其中 `-instancename` 必须保持原值并位于最后。
脚本返回 2 表示采集不完整或前置条件不足；采集完整时返回夹具退出码（包括失败的 3）。
