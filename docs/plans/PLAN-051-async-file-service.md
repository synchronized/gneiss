<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-051：统一异步文件服务

## 状态

实现中。服务核心、内存后端和旧异步读取回执适配已实现，当前行为见[文件服务参考](../reference/file-service.md)。
普通 CPU 资源异步加载已接入 file_service，读完后只提交一次解码任务；asset_source 已删除。
支持宿主注入服务，旧 VFS 构造自动导入挂载并保留缓存来源身份。
桌面完整后端、旧同步加载、网格/纹理专用读取及异步宿主关闭仍待实施。
浏览器、写入持久化和完全统一的文件入口尚未落地。
Windows Clang Debug 全工程编译、格式和边界检查已通过；契约测试已编译但未执行。
大场景、浏览器与 CI 在统一验收时运行。
架构决策见 [ADR-084](../decisions/ADR-084-async-file-service.md)。

## 背景与目标

迁移前 virtual_file_system 只支持 asset://、只读后端和轮询回执，asset_source 编排打开、分块、关闭，
并允许同步回退。目标是统一桌面/浏览器异步文件调用，资源层只处理字节解码和资源缓存。
文件服务吸收挂载路由、请求生命周期、取消、回调分发；平台差异留在后端。

## 非目标

不重引入 CAORS、资源版本事务、通用 Service 基类、新线程池或公共 C ABI。
浏览器不用 libuv 模拟文件系统，不依赖 Fetch destinationPath 落 IDBFS，不依赖 std::filesystem。
不以 IDBFS 承诺任意大文件低内存存储，不提供主线程阻塞等待，不默认增加 JS 桥接或多级缓存。

## 已确认决策

- 类型位于 gneiss::engine，file_service 使用 PImpl，平台类型不进入服务头文件。
- 读、区间读、写、元数据、分页列举、flush 均异步提交；返回值只表示是否接受请求。
- 提交失败不回调；接受后在正常服务生命周期内恰好一次终态回调，缓存命中也禁止内联回调。
- 所属线程提交、取消、配置和分发；后台仅提交完成数据。回调可提交/取消请求，不允许递归分发。
- URI 在提交时复制；请求拥有读结果和写缓冲；回调不得依赖过期对象，业务对象使用弱引用。
- 请求 ID 防止跨服务混用和旧 ID 重用；丢弃 ID 不取消操作。
- 取消与完成只选一个终态，取消不能撤销已发生的写入；关闭时拒绝新请求，等待实际后端收尾。
- 析构不阻塞也不调用用户回调；正常关闭必须持续分发到 shutdown 回调。
- 初始化阶段配置挂载，首次接受请求后冻结；最长前缀选挂载，读取链仅 not_found 自动回退。
- 写入只路由到一个后端，失败不改投；写后需要立即可见时，可写后端必须排在读取链前端。
- stat/list 遵循读取覆盖顺序，不合并多个目录；分页不承诺快照，HTTP 列举需 manifest 或服务器支持。
- 能力不足返回 unsupported；长度未知用 optional，零长度不能代表未知。
- flush 针对挂载持久化域，覆盖此前已完成写入；写入完成不等于持久化完成。
- 文件缓存保存原始字节，asset_manager 缓存对象；缓存需要容量、失效策略，写成功使对应字节缓存失效。
- 普通整读有上限；范围读限制长度；流式采用异步 read_next 和有界预读，不无限积压块。
- 路径采用规范化 URI 和相对路径，拒绝 ..、反斜杠、NUL 与重复分隔符，避免后端各自解释路径。

## API 方向（规划）

```cpp
result read(uri, read_options, file_callback<file_bytes>, file_request_id&);
result read_range(uri, file_range, file_callback<file_bytes>, file_request_id&);
result write(uri, file_bytes, write_options, completion_callback, file_request_id&);
result stat(uri, file_callback<file_info>, file_request_id&);
result list(uri, list_options, file_callback<directory_page>, file_request_id&);
result flush(mount_point, completion_callback, file_request_id&);
result cancel(file_request_id);
void dispatch_completions(size_t max_callbacks);
result shutdown(completion_callback);
```

具体实现签名以源码为准。后端接收相对路径和类型明确的回调；可立即完成或后台完成，
用户回调仅由服务统一分发。已有轮询型 libuv 回执通过内部非阻塞推进适配，禁止同步回退。
服务限制作业数量和每次整读/写入大小；总缓冲预算、流式背压在对应阶段补齐。

## 实施顺序

1. **核心闭环（已实现，运行验收待办）**：错误码、请求 ID、延迟分发、取消/关闭、读写路由、内存后端；编译契约测试。
2. **桌面接入**：复用 libuv 异步打开/读/关闭；增加异步写/stat/list/flush，移除该路径 std::filesystem。
3. **资产迁移（进行中）**：已完成服务注入、普通异步加载、删除 asset_source 与该链路同步回退，
   宿主通过 advance_resources 驱动文件完成，导入挂载保留缓存来源身份。
   待迁移显式同步加载、网格/纹理区间读取、宿主异步 shutdown；不以同步等待桥接新服务。
4. **浏览器闭环**：Fetch 远程读取；FS 内存操作；IDBFS mount 后 syncfs(true) 完成才 ready；
   write 后显式 syncfs(false) 持久化，同域串行同步。处理 JS 异常并映射 result。
5. **大文件与缓存**：严格校验 HTTP Range 响应，限定在途块与总预算；锁定 SDK 验证流式能力；
   加入按需字节缓存，明确更新、失效、离线回退，不缓存文件错误为成功。
6. **可选 JS 入口**：有实际调用方时包装回调为真 Promise；取消也必须 settle，不依赖 Asyncify/JSPI。

## 测试与验收

- 统一后端测试：空文件、范围边界、上限、短读、缺失、拒绝写、元数据和分页；支持/不支持能力明确。
- 路由测试：最长前缀、not_found 回退、io/unsupported 不回退、唯一写入、写后读取优先级。
- 生命周期测试：不内联、预算分发、重入、跨服务 ID、取消竞态、迟到完成、关闭清理、后台完成线程。
- 桌面：所有文件操作不在提交/分发线程执行阻塞文件 I/O；正常/失败路径都等待关闭收尾。
- 浏览器：初始化恢复顺序、flush 失败、容量不足、Range 不支持、CORS、缓存失效、关闭期间回调。
- 大文件：应用缓冲和 JS/Wasm 复制峰值纳入预算，消费者慢时不会无限堆积；IDBFS 限制单独说明。
- 当前只编译测试代码与主工程，运行结果不得标记为通过。

## 风险与取舍

统一接口不代表各后端能力相同。HTTP stat 可能缺少长度，list 需要清单；不会伪造元数据。
FS 的同步内存操作仍有复制开销，包装回调不等于后台执行；大操作需要 Worker 或分块。
IDBFS 依赖内存文件系统，持久化并不保证无限容量或永久免于浏览器清理。
现有资产同步接口与任务执行器需要集中迁移，避免 worker 等待 owner 回调形成死锁。
取消后端 I/O 属尽力而为；已接受但无法中止的请求须继续保活至实际完成。

## 参考资料

- [Emscripten FS 与 IDBFS](https://emscripten.org/docs/api_reference/Filesystem-API.html)
- [Emscripten Fetch 配置](https://emscripten.org/docs/tools_reference/settings_reference.html#fetch-streaming)
- [异步 JS 互操作](https://emscripten.org/docs/porting/asyncify.html)
- [libuv 文件 API](https://docs.libuv.org/en/v1.x/fs.html)
