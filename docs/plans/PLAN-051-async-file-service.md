<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# PLAN-051：统一异步文件服务

## 状态

实现中。服务核心与内存后端已迁入 platform/files，来源标记已通用化；
Platform 不再依赖旧 VFS 或资产层。独立 libuv 桌面后端已实现读、范围读、写、stat/list/flush。
Windows 本地服务契约、桌面后端和资产管理小范围测试通过；Application 已直接装配 file_service 与 libuv 后端。
asset_manager 的旧 VFS 构造、files() 和 load_sync/load_sync_erased 已删除，统一注入文件服务。
旧适配器仅保留在 tests/support，生产引擎不再编译它；旧 VFS 与同步准备器本体仍待删除。
asset_manager 已提供 find_loaded；普通 CPU、Mesh/Texture 异步加载已使用文件服务。
宿主已统一分发文件完成，Manager、Scene 和 Texture Loader 不再隐式驱动共享服务。
服务已增加在途及待分发字节缓冲的总预算；已知长度文件支持按需单块读取，无隐式预读。
Fetch 与 MEMFS/IDBFS 后端已实现，Emscripten 5.0.6 编译及本地 Edge 小型持久化/范围读取测试通过。
Application 的浏览器分支按 asset_root 装配后端：单斜线开头的绝对路径借用 Emscripten FS 已有目录，
其他路径作为 Fetch URL 基址；已有目录不由后端卸载。浏览器后端的借用生命周期、缺失路径和符号链接
拒绝，以及原有 IDBFS/Fetch 小型测试通过；该宿主分支尚未完整构建验证。
浏览器本地持久化挂载、存储异常、未知长度下载及平台复制峰值仍待验证或实现。
输入映射已删除同步 load_input/load_action_map，示例和测试改用异步请求。
Scene/Prefab 准备层已删除同步 prepare_scene_description/prepare_prefab_description，
统一使用 scene_description_request 加载输入和 prepare_loaded_scene 纯内存准备。
四个场景示例已改用异步请求及显式激活；属性检查示例的加载、保存、重新加载小闭环通过。
异步纹理解码直接消费描述/负载字节，不再建立临时 VFS；旧同步准备器仍待删除。
Application::reload_render_assets 和 render_asset_loader::reload_assets 已删除，
重载调用方统一使用 request_assets/poll_assets 或 texture_load_service；acquire 已改为纯缓存查询。
资产管理器已支持请求停止和非阻塞收尾；运行中解码、迟到 I/O 及停止后拒绝加载的小型测试通过。
Application 已新增分阶段关闭入口，串联 Scene、资产管理器和文件服务；
空闲、场景读取中、资源读取中的线程池/协作模式小型关闭测试通过。
Runtime/Editor 正常事件循环退出已接入分阶段关闭；Editor 先释放文档/UI 借用，再收尾文件与 CPU 任务。
启动失败路径、Render 最终销毁和析构仍有同步兜底。
Runtime 的 Scene/Prefab 结构重载已改用异步描述/依赖准备，完成后原位更新节点；IPC 仅在完成后更新修订。
结构重载的 Scene/Prefab 实例代码只查询已准备资源，不同步读盘；成功、依赖缺失、节点身份与 IPC 失败语义的小型测试通过。
编辑器文件监听已改为请求异步场景加载，成功后才通知 Runtime 并确认指纹；加载期间保留后续事件。
编辑器作者文档历史已持有修改前后的内存快照和资源强引用，通过内置 codec 解析变更，
撤销/重做不再重新读盘；文件事务仍使用 Editor 现有保存实现。
编辑器复制 Prefab 已使用异步编辑入口，旧 asset_scene_commands 同步命令已删除；
子树删除/复制与 Mesh Renderer 移除的撤销持有资源强引用，恢复只消费内存或查询缓存。
Scene 首次同步 load、Document 同步 open 及其同步准备实现已删除；测试通过专用宿主驱动异步请求，
场景域测试直接消费已准备描述和资源引用。Prefab 旧同步创建、恢复、刷新及刷新 token/事务表已删除，
测试改用异步编辑命令及持有资源的历史快照，创建、覆盖保留、删除与撤销重做的小型测试通过。
Render acquire_* 已收敛为纯缓存查询，缺失不再同步准备或读盘；导入节点测试与历史改走异步编辑命令。
异步渲染加载回执保留解析错误路径和字节偏移。
环境贴图启动读取已改用文件服务：先使用内置环境，读取完成后由渲染执行器采用字节；
读取失败在宿主推进时报告，解析失败继续使用内置环境。此 GPU 更新路径已编译，尚未运行画面验证。
Render 的 prepare_render_assets 同步入口已删除；准备器测试临时通过测试宿主异步加载材质。
底层旧同步准备器、删除 VFS、浏览器和大文件能力仍待完成。
不将 Platform 搬迁或桌面后端实现等同于整份计划完成；大场景和 CI 仍留待统一验收。
架构决策见 [ADR-084](../decisions/ADR-084-async-file-service.md)。

## 当前五项收尾进度

- Runtime/Editor 事件循环正常退出已采用分阶段关闭；启动失败和析构兜底尚待收敛。
- Scene/Prefab 结构重载、编辑器文件监听、作者历史恢复已迁移；首次同步 load/open 和旧 Prefab 编辑辅助 API 已删除；Mesh 编辑辅助 API 尚待收敛。
- Render 首次 acquire 的同步准备回退已删除；缓存缺失统一由异步请求处理。
- Application 已直接装配 Platform 后端并删除 asset_files；Manager 同步入口和 VFS 构造已删除。
  同步准备器、旧 VFS 及测试专用兼容夹具仍待删除。
- 当前改动已通过 Windows 编译、相关小型测试及 Core 分层检查；完整计划验收尚未开始。

## 本轮确认范围（待实施）

本轮将文件抽象整体收敛到 `engine/platform/files`，由它完整替换旧 VFS。
文件操作与资产加载只提供异步入口，不保留同步工具入口、同步回退或阻塞等待包装。
纯内存解析、序列化、缓存查询和已准备数据的场景实例化仍可同步执行。
本节描述完整目标；核心已迁入 Platform，原 resource/files 的适配器已移出生产树；其他迁移状态见上节。

### 对象与依赖方向

- Application 统一装配文件服务、平台 I/O 执行器、CPU 任务执行器与资产管理器。
- file_service 管理挂载、读取回退链、唯一写入后端、请求、取消及所属线程完成分发。
- file_backend 只处理路径和字节；桌面、浏览器与内存实现不理解资产或反射。
- asset_manager 管理解码器、在途加载去重、资源引用与唯一缓存；必要的依赖编排仍由资源 Loader 负责。
- 宿主统一调用 dispatch_completions；asset_manager 不再隐式驱动共享文件服务。
- Scene 在必要 CPU 数据准备好后同步创建实例，不恢复跨帧构建、候选发布或跨资源事务。
- Render 生命周期和加载成功语义沿用 [PLAN-054](PLAN-054-render-resource-model.md)：
  加载只承诺本次请求的 CPU 内容就绪，不等待 GPU 上传，不将上传进度加入文件请求。

依赖方向为 Resource → Platform → Core。Platform 不得包含 Resource、Meta codec、Scene 或 Render。
现有边界检查保持严格，不通过放宽规则允许反向依赖。

### Platform 迁移前置条件

迁移前 file_service 导入 virtual_file_system 挂载并使用 Resource 的 asset_source_domain。
来源标记已通用化，原 Resource 适配函数已移至 tests/support，仅兼容未迁移的测试夹具。

文件层已定义不透明的 `file_source_domain` 挂载来源标记，由挂载表创建并持有。
`resolve_source` 只解析来源，不执行 I/O；Resource 的 asset_identity 引用该标记，
URI、类型与缓存规则仍由 Resource 解释。该标记不是持久 AssetId、内容版本或文件后端所有者。
不同来源不能因 URI 相同命中同一资产，持有标记也不应延长后端或 GPU 生命周期。
删除旧 asset_source_domain 定义，避免为同一挂载维护两套身份；迁移期身份转换仅存在于上层适配中。

旧 VFS 导入构造和适配器在调用方迁完后删除，不随文件服务下沉到 Platform。
通用路径校验迁入文件层；仅适用于 asset:// 的校验继续留在 Resource，不把资源协议限制扩散到所有文件挂载。

### 目标目录（规划）

```text
src/engine/
  platform/
    files/
      file_service.hpp/.cpp
      file_backend.hpp
      file_types.hpp
      file_path.hpp
      memory_file_backend.hpp/.cpp
      native_file_backend.hpp/.cpp
      browser/
        fetch_file_backend.hpp/.cpp
        browser_file_backend.hpp/.cpp
    io/uv/                         # 复用平台执行器等设施
  resource/
    asset_manager.hpp/.cpp
    asset_request.hpp
    asset_ref.hpp
    asset_uri.hpp/.cpp              # 资源专用 URI 规则
    cache/
    loaders/
    types/
```

类型仍位于 `gneiss::engine`，目录不增加命名空间层级。服务及平台后端通过 PImpl 隐藏第三方类型。
后端由宿主装配；服务核心不依赖具体后端。同步更新 CMake 源文件、头文件可见性和测试包含路径。
最终删除 resource/files、resource/vfs 和 async_read_file_backend；平台底层回执仅在确有内部调用时保留，
不再向资源调用方暴露旧 open/read/close 协议。

### 资产 API（规划示意）

```cpp
template<class T>
result load(std::string_view uri, asset_request<T>& request, bool reload = false);

template<class T>
result find_loaded(std::string_view uri, asset_ref<const T>& output);
```

复用现有 asset_request<T>；take() 只检查并领取结果，未完成返回 not_ready，不驱动队列、不等待 I/O。
find_loaded 只查询已完成缓存，未命中返回 not_found，失败保持输出；不读盘、不解码、不注册加载任务。
保留显式 decoder 优先、Meta codec 回退、纯内存 store 和资源强引用规则，不强制所有资源注册属性反射。
取消资产请求仍表示放弃本观察者，不影响共享加载的其他观察者。

删除 load_sync、load_sync_erased、同步 load_input、旧 VFS 构造、files() 旧访问器、legacy_sync 与同步回退。
普通文件回调不得内联调用用户代码；缓存查询是明确的同步内存 API，与异步 load 分开。
场景打开、Prefab 依赖、编辑器保存和重载均迁移到完成后继续的流程；
保存完成后更新保存状态，持久化承诺依赖 flush。命令行工具仅由最外层驱动事件循环至任务完成。

### 宿主关闭（规划）

停止业务提交 → 资产管理器停止接收并取消/收尾任务 → 文件服务 shutdown →
宿主持续分发文件完成并收尾 CPU 任务 → 文件关闭回调完成 → 停止平台执行器。
资产关闭不能阻塞所属线程等待依赖该线程分发的任务。解码执行器须存活至最后一个解码任务结束。
共享服务仅由宿主关闭；析构不能替代正常关闭协议，迟到回调不得访问失效业务对象。

## 背景与目标

迁移前 virtual_file_system 只支持 asset://、只读后端和轮询回执，asset_source 编排打开、分块、关闭，
并允许同步回退。目标是统一桌面/浏览器异步文件调用，资源层只处理字节解码和资源缓存。
文件服务吸收挂载路由、请求生命周期、取消、回调分发；整套通用文件能力归 Platform，资源语义留在 Resource。

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
result dispatch_completions(size_t max_callbacks);
result shutdown(completion_callback);
```

具体实现签名以源码为准。后端接收相对路径和类型明确的回调；可立即完成或后台完成，
用户回调仅由服务统一分发。已有轮询型 libuv 回执通过内部非阻塞推进适配，禁止同步回退。
服务限制作业数量、每次整读/写入大小及字节缓冲总预算；流式背压在对应阶段补齐。

## 实施顺序

1. **已有核心复用**：保留服务核心与内存后端，审计请求、错误、取消、来源身份和关闭契约。
2. **独立桌面后端**：直接接入平台 libuv 执行器，补齐读、范围读、写、stat、list、flush，
   不经过旧 file_system，不依赖 std::filesystem，不新增线程池。
3. **调用方迁移**：迁移宿主挂载与完成分发、asset_manager、Scene/Prefab、编辑器、工具、示例和测试；
   增加 find_loaded，移除同步加载及工作线程等待；内存解码不再创建临时 VFS。
4. **关闭与分层收敛**：完成异步关闭，来源身份通用化，搬入 platform/files，删除 VFS 和过渡适配器。
   若分步实施需要临时桥接，桥接只允许在 Resource/宿主侧，不让 Platform 反向依赖旧 VFS。
5. **浏览器闭环**：Fetch 远程读取；FS 内存操作；IDBFS 恢复完成才 ready；
   写入后显式持久化，同域串行同步；错误统一映射 result。
6. **大文件闭环**：严格校验范围响应，补齐有界流式读取、背压与总缓冲预算，验证复制峰值；
   不把范围读取等同于 mip/LOD 流送，也不承诺 IDBFS 任意大文件低内存存储。
7. **按需扩展**：字节缓存及 JS Promise 仅在实际调用方需要时增加，不作为删除 VFS 的前置条件。

阶段 1—4 完成代表桌面 VFS 替换完成；浏览器和大文件核心范围仍属本计划，不能提前标记整份计划完成。

## 测试与验收

- 源码、构建与活跃测试无旧 VFS、同步资产加载、迁移适配器入口；历史记录不改写。
- Platform 不包含 Resource，Resource 通过唯一文件服务访问存储；来源身份及缓存隔离正确。
- 场景准备缺失依赖时等待异步加载，实例化不临时读盘；不恢复版本发布或跨帧实例构建。
- 宿主正常退出等待文件和 CPU 任务收尾，最后才停止执行器；调用方销毁后没有失效访问。

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
