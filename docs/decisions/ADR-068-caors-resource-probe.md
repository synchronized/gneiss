<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# ADR-068：CAORS 单资源接入验证边界

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
