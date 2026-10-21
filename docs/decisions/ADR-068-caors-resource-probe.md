<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-068：CAORS 单资源接入验证边界

目录与组装职责后续以 [ADR-069](ADR-069-resource-runtime-boundaries.md) 为准；
本记录已有接入及平台契约保留，function/asset 不是最终目录。

## 状态与背景

2026-10-20，采用；已从独立验证扩展至默认关闭的 Application 输入接入。用户要求先接通一个真实 CPU 资源，
验证 CAORS 资源管理与现有 libuv、调度器的组合，完整 VFS 迁移仍按
[C51-07](../plans/UPSTREAM-051-caors-async-vfs.md)推进。

## 决策

- 新增默认关闭的 `GNEISS_BUILD_CAORS_PROBE`，CAORS 0.4.0 固定提交，MIT 许可。
- CAORS 适配位于 `src/engine/platform/caors`，以内部静态库 `gneiss_caors_adapter` 编译，
  独立适配库仅验证 EXE 链接；可选 Engine 集成直接编译所需实现，不重复链接 InputService。
  CAORS 头文件和对象不进入 Gneiss 普通公共接口。
- 唯一资源缓存由 CAORS cache 提供，交给 asset_manager 注册管理；不再创建平行缓存。
- 用 Gneiss task_executor 的独立 scope 适配 external_executor；主线程 completion 队列不创建线程。
- 首个资源使用既有 `parse_action_map` 解析为强类型 action_map，校验键值和绑定规则；
  解析器与旧 VFS 的同步加载函数分离，共享相同实现；资源就绪后仅由宿主显式 poll 应用到 InputService。
- 独立探针保留 root 为空的直接路径后端，用于验证 CAORS 协议；Application 使用现有
  Gneiss VFS 快照桥。打开通过 request_open / start_open，读取通过 async_file。
  初次读取不阻塞 worker 等待 I/O；发布前的有界复读当前在 worker 同步完成，
  不在主线程读取。该复读是变化检测，不提供跨进程原子快照。

## 限制与后续

打开、范围读取与显式关闭已有验证适配，尚未统一生产 VFS。
libuv 显式关闭拒绝新读并等待在途读结束，实际关闭回调结束后发布结果，重复关闭共用终态。
验证适配需要宿主继续 pump；放弃推进前调用 abandon 解除完成凭证保活环，再排空 loop。
实验有 1 MiB 文档限额，不是纹理流式方案。取消先中止逻辑加载并放弃回执，
不能声称强制中断了已提交的操作系统 I/O。
异常退出也必须先关闭工作 scope、放弃回执并排空 loop，再销毁 Loader/manager。

平台适配以 `.cpp` 隐藏实现，仅内部头文件暴露宿主 pump/abandon/collect 与工厂；
平台模块不依赖输入、场景或其他业务资源。输入资源 Loader 与待应用对象位于 function/input，
由可选私有目标 gneiss_caors_input 构建，当前仅独立宿主链接。
待应用对象只保存 CAORS 请求订阅，不建缓存，不由后台持有 InputService；新请求取消旧订阅，
仅最后请求可生效。成功应用一次，失败和取消保留现有映射及 action_id。
可选 Application 宿主生命周期已接入；现有同步公共 API 与 Editor 启动路径尚未切换。
替代方案是一次替换所有资产路径，风险更大，且无法定位协议与生产迁移的问题边界。

## 2026-10-20：Application 接入顺序

用户确认采用[方案 A](../plans/PLAN-051-caors-application-integration.md)：现有 Gneiss VFS
保持唯一挂载权威，资产层临时桥持有其快照，并透传来源身份、异步打开及显式关闭。
不将 asset_root 再挂到实验直接路径后端；平台适配不反向依赖资产层。
桥接完成后才接入 Application，之后迁移挂载权威时删除过渡桥。
采用默认关闭的 `GNEISS_ENABLE_CAORS_INPUT` 验证真实 Application；不扩张公共 C ABI。
Engine 内唯一拥有输入 CAORS 状态，EXE 只通过 Gneiss 原生接口调用。
启用时安装固定 CAORS 依赖包，静态 Gneiss consumer 通过 find_dependency 获得链接依赖；
共享 Engine 内静态链接 CAORS。未启用时不增加 SDK 依赖。
现阶段只接受原生线程/libuv 模式，Emscripten 或关闭线程的配置明确拒绝启用此选项。

## 2026-10-20：共享材质的分步接入

用户确认[共享材质方案 A](../plans/PLAN-051-caors-shared-material.md)：CAORS 先管理 CPU
材质文档，Gneiss 保留确切纹理版本、RID 发布和场景采用事务。两者管理不同阶段的对象，
不得建立两个独立决定当前生效材质版本的缓存。
共用有界文档加载协议放在私有平台适配中，以回调接收 CPU 解码与复验；材质格式仍属于
资产层。平台适配不依赖材质、Scene、World 或渲染服务，不自行创建线程或资源管理器。
强引用保留旧对象与固定内容版本缓存是不同能力；生产接入必须补齐来源域和版本映射，
不得把输入宿主的 URI 到 GUID 映射直接当作共享材质版本协议。

生产材质准备继续复用现有批次异步读取及整批复验，通过不含 CAORS 类型的解码回调承接
CPU 文档。独立 Loader 与批次回调共用解析实现，不在已有批次读取后再次按 URI 加载。
该回调只负责候选解码，不能提前发布共享版本或访问渲染服务。

材质生产通道由 asset_manager 持有一个 CPU 文档索引，各请求只创建独占候选批次，
不复制输入宿主的 manager、VFS 或线程池。候选在准备线程解码，任务完成并确认未取消后，
所属线程按已复验来源登记文档租约；渲染材质缓存项持有租约，旧场景/撤销引用继续保活旧文档。
跨层仅传递 Gneiss 自有的文档生命周期接口，CAORS handle 不进入渲染或普通公共接口。
同一材质在两批中仍需各自读取、解码和复验，共享发生在复验后，不承诺消除解析开销。
使用独立且默认关闭的 GNEISS_ENABLE_CAORS_MATERIAL 开关；同步 acquire 路径保留原解析行为。

## 输入资源所有权收敛

Application 只拥有 asset_manager 与 InputService；输入资源宿主（manager/cache/Loader、
URI 映射及在途订阅）由 asset_manager 的私有输入状态拥有。更新阶段由 Application 提供
采用回调，资源宿主不直接引用 InputService；回调成功后才报告输入就绪，失败仍保留旧映射。
关闭继续先取消并排空输入 scope，再关闭场景通道、资产管理器和 I/O 后端，借用的执行器不关闭。
本轮统一所有权，不把输入的可变 URI reload 与材质的不可变内容版本索引强行合成同一协议。

## CAORS 共享运行时

后续接入统一构建 CAORS 共享库，取代此前 Engine 内静态链接方式，避免 Engine、工具与探针
各自静态复制全局状态。Engine 静态/共享构建均使用同一选择，不扩张公共接口或插件 ABI。
Windows 构建输出统一到 bin，安装由 CAORS 自身规则提供 DLL/导入库及 package；
静态 Engine 的 consumer 仍通过 find_dependency 解析链接依赖。Unix 共享 Engine 的安装
搜索路径增加同目录，以匹配两个库同在 lib 的部署布局；平台行为须分别验收。
仍不承诺跨编译器 C++ ABI 兼容：CAORS 与宿主须使用一致工具链和运行库。

## Mesh CPU 资源接入

将材质专用批次入口提升为 cpu_document_cache，仍由 asset_manager 唯一持有；
通过独立且默认关闭的 GNEISS_ENABLE_CAORS_MESH 接入网格，不另建宿主、线程池或 VFS。
Mesh 继续由原准备器读取、解析、校验和整批复验；成功后把数组移动到 CAORS 资源，
按来源域、URI、摘要和输入大小共享同一不可变 CPU 数据。弱索引不选择当前渲染版本。
渲染层接收不含第三方类型的 shared_ptr<const mesh_data>，别名所有者保活 CAORS handle；
RID、上传候选及旧帧共享该对象，释放 RID 不提前释放旧帧数据。不复制整份 Mesh 数组。
同内容的后续批次仍需读取、解析及复验，不将此项描述为加载加速。
同步加载、未注入缓存的通道保留原路径；纹理载荷迁移不包含在本步中。

## 2026-10-21：CPU 资源基础依赖与平台 I/O 分离

CAORS 核心成为必选依赖，删除材质/Mesh 开关和关闭实现；统一 CPU 缓存不依赖 libuv。
原生平台采用共享 CAORS，Emscripten 采用静态 CAORS；静态 Gneiss 安装包始终解析 CAORS 依赖。
直接 libuv 探针适配库仅在构建探针时装配。输入宿主暂保留原生条件，直到协作 VFS 打开、
读取、显式关闭协议完成；不得把核心可编译描述为输入异步路径或浏览器 I/O 已完成。
手工未注入缓存的底层通道及同步 acquire 仍待按收敛计划合并，不能声称全部双路径已消除。

## 2026-10-21：有界输入文档的协作调度

输入宿主不再依赖 libuv loop 是否存在。VFS 桥优先使用原异步打开路径，仅在返回
unsupported 且调用者明确允许时，将有界完整文档读取投递到同一执行器。
结果是只读内存快照，复用既有 CAORS Loader、版本复验、缓存、取消及主线程采用流程。
不新增资源管理器或挂载表，不将快照关闭冒充底层文件异步关闭。

协作执行器由宿主显式推进；其中的同步文件读取可能阻塞推进线程。该适配仅用于有大小
上限的小型 CPU 文档，不代表浏览器 fetch、纹理流送或可抢占 I/O 已实现。
入队阶段与桥的 pump 不执行同步读取；执行器拒绝或丢弃任务必须产生失败终态。

## 2026-10-21：输入成为必选资源实现

原生无线程与 Emscripten MEMFS 的 Application 输入闭环验证通过后，删除输入功能开关，
Engine 始终构建输入宿主。Runtime 线程池与协作调度均走请求、完成、采用流程，
不再因协作模式或旧开关关闭而回退到同步启动。直接 libuv 探针仍受平台能力限制，
Application 输入测试则独立运行于无线程平台。浏览器网络 I/O 不包含在此次决策中。

## 2026-10-21：资源管理归资产层

用户确认输入模块不应拥有 CAORS asset_manager。删除 function/input/caors_input_host，
将 manager、类型缓存、来源绑定、GUID 分配、执行器适配和完成队列直接收归
engine::asset_manager 的私有 resource_state。该状态不向公共 SDK 泄漏第三方类型。

输入模块提供 action_map_asset、格式 Loader 和待采用状态；待采用状态只接收资产层创建的
load_request，不再接收 manager，也不决定 load/reload 策略。InputService 仍只接收映射副本。
Application 继续决定采用时机与游戏启动顺序。

销毁顺序保持为取消采用、关闭并排空专属工作 scope、放弃 Loader 回执、排空完成队列，
然后释放资源状态；不关闭借用的整个宿主执行器。当前该状态注册输入类型，其他 CPU 缓存
仍按既有协议持有；本决策不宣称所有资源类型已统一到一个 CAORS database。

## 2026-10-21：Application 采用资源，项目设置引用配置

不引入进程全局 locator<asset_manager>；Application 保持服务所有权，内部依赖显式传递。
继续使用现有项目 input_map 字段引用独立输入文件，不新增第二份项目设置文件或内嵌配置分支。
输入服务不定位资产管理器、不发起加载。

资产层提供最小强类型 action_map_request，隐藏 CAORS 请求；take 只返回映射副本，
不调用 InputService，也不决定游戏启动。当前只实现输入这一实际用途，不提前推广类型擦除框架。
请求由 Application 的 action_map_application 持有，负责替换、取消、一次采用和采用状态。
它不持有 CAORS 类型；资产层只负责请求、缓存、来源和队列推进。

Application 退出先释放待采用请求，再停止资产工作；同步加载取消当前采用由 Application
完成。同一进程不同 Application 继续拥有各自服务，不依赖可切换的全局默认对象。
