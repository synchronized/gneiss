<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 异步文件服务

## 当前范围

内部原生入口位于 `src/engine/platform/files/file_service.hpp`，命名空间为 `gneiss::engine`。
当前实现 Platform 服务核心、内存后端和独立 libuv 桌面后端；旧异步读取适配仍留在 Resource。asset_manager 的普通 CPU 资源异步读取
已接入服务，文件完成后才提交一次解码任务；旧 asset_source 已删除。
旧 VFS 同步加载、网格/纹理的专用范围读取与宿主同步关闭仍待后续迁移。
已实现已知长度文件的按需分块读取，以及 Fetch 与 MEMFS/IDBFS 浏览器后端。
字节缓存、JS Promise 尚未实现；Application 尚未切换独立桌面/浏览器后端。
后续迁移见[开发计划](../plans/PLAN-051-async-file-service.md)。

## 对象与所有权

- `file_service` 为不可复制/移动的 PImpl 对象。构造线程为所属线程。
- 服务拥有挂载后端的共享引用、在途请求及用户回调；请求 ID 仅用于取消。
- `file_backend` 是后端扩展接口。提交和 poll 由所属线程驱动，完成可来自任意线程。
- 后端成功接受操作后恰好调用一次完成回调；提交失败不得回调。后端自行保活异步数据。
- 服务复制 URI，按值接收写缓冲；完成结果拥有字节，调用方可将其移动到解码任务。
- 后端可能内联完成，但用户回调始终延迟到服务分发。

## 操作与限额

| 接口 | 当前语义 |
| --- | --- |
| read | 有界整文件读取，max_bytes 必须大于零 |
| read_range | 精确范围，允许 EOF 处零长度，越界失败 |
| write | replace 或 create_new；不承诺跨后端原子覆盖 |
| stat | 文件/目录及可选字节数；未知与零长度不同 |
| list | 当前目录分页，不递归、不合并后端、不保证快照 |
| flush | 仅接受精确挂载点，调用唯一写后端的持久化能力 |
| cancel | 尽力取消观察结果，不回滚写入、不保证中止实际 I/O |
| dispatch_completions | 非阻塞推进后端并按回调数量预算分发 |
| shutdown | 拒绝新请求并收尾已有请求，最终通过分发通知关闭 |

默认最多 64 个未分发请求，整读/范围读/写入单次最多 16 MiB，列举每页最多 1024 项。
已完成但未分发的结果也占用请求容量；满载提交返回 not_ready。
默认总字节预算为 256 MiB，可用 max_buffer_bytes 配置。整读按 max_bytes 预留，范围读按 size，
写入按缓冲大小预留；单请求超总预算返回 invalid_argument，并发预算不足返回 not_ready。
取消请求须等待后端收尾才释放预算，已完成未分发的字节继续占用；在调用用户回调前释放预留。
该预算覆盖服务的字节负载，不包括消费者接收后的数据、目录扫描元数据和平台内部复制。
分发预算只限制用户回调数量，不是精确 CPU 时间预算。内存后端复制字节仍有成本。

## 分块读取

`file_read_stream` 持有服务、URI、已知长度和块大小；长度通常来自异步 stat。
`read_next` 最多接受一个在途块，消费者再次请求才继续，无隐式预读。
完成块包含 offset、拥有的 bytes 和 eof；空文件交付一次空 EOF。
失败不推进位置，可重试；析构取消观察而不等待 I/O。
每块受服务字节预算约束，但消费者保留的旧块不计入预算；不提供跨块文件快照。
未知长度 HTTP 响应的流式下载与浏览器内部复制峰值尚未解决。

## 路由与路径

挂载前缀以 `/` 结尾，例如 `asset://`、`asset://textures/`、`user://`。
scheme 仅允许小写英文字母；路径须有效 UTF-8，拒绝控制字符、反斜杠、百分号编码、查询、片段、
冒号、`.`、`..` 和空路径段。挂载根与目录允许尾部 `/`。
首次接受请求后冻结挂载；最长前缀决定挂载，读取链只在 not_found 时继续。
write/flush 只选择唯一 writer；缺少 writer 时提交返回 unsupported。

分页游标为服务生成的不透明字符串，只能原样用于同一服务、同一路径。
后续页固定到原读取后端，不因覆盖链中新出现目录而切换；跨路径游标提交失败。
游标不是持久化数据，不得保存到文件或跨服务实例使用。

## 完成、取消与关闭

提交失败清空请求 ID，不回调；接受成功返回非零 ID，错误通过终态回调报告。
未知挂载属于提交失败；已接受后后端缺失文件、能力不足、I/O 错误属于完成结果。
失败结果不返回部分字节。回调需不抛异常，可提交或取消其他请求，但不能递归分发或销毁服务。
本轮回调中新提交的请求留到下次分发。

ID 在进程内单调分配、不重用，服务只接受自己仍持有的 ID。
完成先到时 cancel 返回 invalid_state；取消先到时等待后端实际结束后报告 cancelled。
后端读取链中的 not_found 不是整体终态，尚未开始的回退可取消。

shutdown 取消尚未完成请求，已有终态照常交付。关闭回调遵守分发预算且不内联执行。
宿主必须继续驱动完成分发及平台 I/O，直到关闭回调，再停止平台执行器。
直接析构只放弃用户通知、不阻塞等待；在途后端自行保活，不向已销毁服务访问数据。

## 已有后端

`memory_file_backend` 提供 read/read_range/write/stat/list，写入隐式建立父目录。
不能用文件覆盖非空目录，也不能在文件下创建子项；数据仅保存在内存，flush 返回 unsupported。
其操作在所属线程执行，不涉及磁盘或网络。

`native_file_backend` 直接使用 libuv 异步文件 API，支持 read/read_range/write/stat/list/flush。
借用宿主执行器，不自行建线程；根目录和写入父目录须已存在，不依赖 std::filesystem。
写入完成前 fsync 并关闭文件，因此 flush 覆盖此前已完成写入；不承诺跨文件原子性或目录项崩溃恢复。
每次操作解析实际路径并拒绝根目录外目标；不承诺抵抗并发恶意符号链接替换。
列举普通文件和目录，特殊条目返回 unsupported；分页不提供快照，底层 scandir 仍可能枚举整个目录。

`fetch_file_backend` 使用浏览器主线程 Emscripten Fetch，提供只读 HTTP GET/HEAD。
范围请求要求匹配的 206、Content-Range 和字节数；返回整文件 200 时报告 unsupported，
不以整读模拟范围。跨域服务器须暴露 Content-Range 等响应头；不使用 destinationPath。
整读仍经过浏览器和 Emscripten 的缓冲与复制，不保证这些额外缓冲受服务预算限制。

`browser_file_backend` 管理独立 MEMFS 或 IDBFS 挂载，仅在浏览器主线程使用。
initialize 的恢复回调成功后才接受访问；内存操作可内联完成，服务仍延迟交付用户回调。
write 仅确认内存写入；flush 调用当前 IDBFS 挂载的 syncfs(false)，不影响其他挂载。
恢复与 flush 串行，同步期间对本后端的新访问返回 not_ready；失败后可重试。
直接析构不自动 flush；宿主须等待显式 flush 完成，再关闭服务和销毁后端。
在途持久化保活挂载，完成后才允许卸载；路径分量拒绝符号链接。
根目录为独立的一级绝对路径，初始化创建挂载点；文件父目录须存在。

`async_read_file_backend` 适配旧 `file_system` 的异步打开/区间读/关闭回执。
每次读取最多 64 KiB，验证上限和短读；成功及读取失败均等待异步关闭后再报告。
不支持异步读取的后端返回 unsupported，绝不调用同步 open_read/read/read_at 回退。
适配器本身不依赖 std::filesystem；迁移期 native_file_system 仍使用该类型处理根路径，
尚未完成桌面后端去除 std::filesystem 的目标。

## 资产管理器接入

`asset_manager(shared_ptr<file_service>, registry, limit)` 持有宿主注入的同线程服务。
宿主先分发文件完成，再调用 `advance_resources` 推进 CPU 解码及结果登记；管理器和 Loader 不隐式分发文件回调。
Application 主循环及显式场景/资产轮询入口共用同一宿主推进函数，独立消费者须自行驱动服务。
等待文件期间不提交 CPU 轮询任务。
读取回调只捕获操作弱引用，不捕获 manager 或执行器，停止后的迟到完成不能触发解码。
同键请求合并、失败保留旧资源、取消独立观察者以及已存储内容不被旧请求覆盖的规则不变。

文件服务每个挂载具有稳定来源域，`resolve_source` 仅查询身份。
来源标记为 Platform 的 `file_source_domain`，不拥有后端或 GPU 资源。
迁移期 Resource 的 `make_file_service(vfs, options)` 导入挂载时复用旧来源域，
避免旧渲染入口与新异步入口对同一 URI 建立不同缓存身份。
旧 `asset_manager(virtual_file_system, ...)` 构造内部导入服务，保留显式同步加载能力。
新注入构造不提供同步磁盘回退，load_sync 仅复用成功缓存，未命中返回 unsupported。
新增 find_loaded<T> 只查询已完成缓存，未命中返回 not_found，失败保持输出，不读盘或提交任务；
旧同步入口将在调用方迁移完成后删除。

桌面启用 libuv 的构建中，Application 即使使用协作式 CPU 调度也配置异步 I/O loop。
尚无实际异步后端的无线程/浏览器构建返回 unsupported，不再恢复同步文件回退。
文件操作和 CPU 调度线程数是不同配置，不应把 cooperative 等同于所有文件操作都在主线程执行。

当前 stop_resources 仍等待已有 CPU scope 收尾，终止观察者、取消读取并释放服务引用。
宿主持有的共享服务继续由宿主分发并正常 shutdown；独占服务析构放弃通知，在途后端自行清理。
Application 的既有同步 teardown 释放读来源和渲染快照后再停止 libuv loop，
尚未改成逐次轮询的异步宿主关闭流程，不宣称整个引擎关闭已经非阻塞。

Mesh/Texture 的运行时加载也借用该服务；等待文件期间不提交 CPU 任务，
描述、完整负载读取和 GPU 上传保持独立阶段。具体契约见 [资产参考](assets.md)。

## 验证状态

契约测试源位于 `tests/integration/assets/file_service_test.cpp`，目标 `gneiss_file_service_test`。
覆盖延迟分发、路由、分页、范围、容量、取消、关闭、线程交付及异步回执编排。
Windows 本地服务契约、资产管理和 native_file_backend 小范围测试已通过；
桌面测试覆盖真实读写、范围边界、读取上限、缺失文件、重复创建、元数据、列举、flush 和关闭。
Emscripten 5.0.6 + 本地 Edge 小型浏览器测试通过：IDBFS 写入/flush/重新挂载恢复、
范围边界、重复创建、stat/list，以及 HTTP 206、错误 Content-Range、200 回退和读取上限。
浏览器宿主接入、异常关页、存储配额失败与大文件峰值尚未验收。

小型浏览器测试为 `tests/integration/assets/browser_file_backend_test.cpp`，Emscripten CMake 目标
`gneiss_browser_file_backend_test`。生成 JS/Wasm 后可用本机 Chromium/Edge 执行：

```sh
node tests/integration/assets/run_browser_files.cjs <测试.js路径> <浏览器可执行文件路径>
```

脚本需要支持原生 fetch/WebSocket 的 Node，使用随机本地端口和产物目录内独立浏览器配置。
测试配置保留在产物目录以便排查，不使用个人浏览器配置。
