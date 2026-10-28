<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Render 资源、组件与帧提取

Render 的 `reserve_mesh/texture/material()` 与 `create_*` 系列返回 `result<rid>`；
失败结果不含 RID，调用方无需准备输出指针。销毁仍返回 `error`。
`prepare_mesh/material/texture_data()` 返回 `result<shared_ptr<const ..._data>>`，
只准备和登记 CPU 数据，不等待 GPU 上传。资源服务内部创建链同样使用值返回。

`prepare_frame_packet_storage(packet)` 原位复用帧存储并返回 `result<bool>`：true 表示可继续构建，
false 表示本轮跳过，失败结果表示执行错误。`render_service::submit(packet, policy)` 及执行器的
`submit_frame/submit_command` 返回 `result<uint64_t>`，成功值是回执序号；调用方不等待 GPU 完成。
底层 `rid_table::create(type, value)` 返回 `result<Identifier>`，句柄的 generation、类型与所属域校验不变。
关闭流程仍通过 error 和统计参数同时报告错误及资源统计，失败时统计信息也必须保留。

## 资源生命周期

Mesh、Material 和 Texture 的 CPU 内容表由 Application 的 `render_service` 私有实现拥有。
调用方通过 `application::render()` 使用 RID 接口，不能借用资源表。
运行身份、元信息与材质依赖由 Application 的 `asset_manager` 维护，通过 `application::assets()` 借用。
程序化纹理使用 `resources.create_texture(texture_view)`，返回 `result<texture>`；网格与材质使用对应的创建接口。
`reserve_mesh/texture/material()` 返回新预留资源的值结果，`create_mesh/texture` 创建有内容的资源，
`create_material(parameters, textures, material&)` 显式传入纹理强引用。成功输出拥有者，失败输出为空。
不存在外部 RID 接管入口；不同管理器不能接管彼此的资源。
`find_mesh/texture/material(rid)` 只返回本管理器已有对象的引用，未知或已释放对象返回空。
普通文件加载通过同一个管理器的 URI 缓存复用对象。上层提取的 RID 仅用于借用，不得手动销毁资源拥有的 RID。
底层 Render 的裸 RID 创建/销毁接口保留，调用方须自行配对，不能混入强引用所有权。
UI 使用 `assets.find_texture(rid)` 保活已有纹理，不隐式建立资源拥有者。
管理器位于 `engine/function/resource/asset_manager.hpp`；批次加载状态、资源工厂及销毁队列属于其私有实现；更新任务不属于对外接口。
宿主先停止资产加载并销毁管理器，再关闭 Render；资源对象头不声明 Render 服务类型。
原生 `render_service::create_mesh(mesh_view, ...)` 会在调用
期间复制顶点、可选法线、切线、UV1、顶点色及可选 UInt32 索引；调用返回后，调用方可以立即释放源数据。Mesh 至少需要
三个有限值顶点。索引非空时数量必须至少为三个且为三的倍数，所有索引必须小于顶点数；索引为空时
顶点按 Triangle List 顺序解释。法线必须与顶点一一对应、保持有限且归一化；不提供法线的旧 Mesh
使用无光照兼容路径。

切线 XYZ 必须归一化并与对应法线正交，W 为 `+1/-1`，数量必须与顶点一致。
属性以原生 span 表达，空 span 表示未提供；数量、有限值及缺省规则见[资产格式](render-asset-formats.md)。
拥有型 `mesh_data` 重载校验后转移数组所有权，避免再次复制。

运行材质保存线性 RGBA、金属度、粗糙度、法线/AO/自发光因子以及五槽 Texture 强引用，
字段范围与纹理用途见 [Render 资产格式](render-asset-formats.md#material)。
内部 `material_data` 使用 RID 记录后端绑定；取得运行材质引用时同时取得对应纹理的强引用。
`create_material` 接收原生 `material_data`，其 `parameters` 使用与材质描述相同的
`material_parameters`，默认构造提供中性值；纹理槽仍保存 RID。
当前描述支持逐槽 UV0/UV1 与采样器、OPAQUE/MASK/BLEND、Alpha Cutoff 和双面。

`create_texture(texture_view, ...)` 接受二维 RGBA8 像素，并显式区分线性与 sRGB 颜色空间。宽高必须位于
`1..16384`，解码后的紧凑像素总量不得超过 256 MiB；行跨度至少为 `width * 4`，输入缓冲区必须覆盖
最后一行。Resource Service 会逐行复制并移除源数据的行尾填充，调用返回后调用方可以释放像素。
Material 可选引用 Texture RID；Granit 后端按 RID 建立 GPU 镜像并通过内置 Shader 采样。

所有 Render RID 只能交还给创建它们的 Application；RID 校验资源类型、generation 和 Service
domain。销毁、跨 Application 使用、类型混用或重复销毁均返回
`error::invalid_handle`。销毁 RID 后，帧包或调用方已有的强引用仍可维持 CPU 数据，
直到最后一个引用释放；该保留数据继续占用服务内存额度。

## 运行资源身份与销毁

`engine/resource` 下各资源目录的 `mesh_resource.hpp`、`texture_resource.hpp`、
`material_resource.hpp` 分别定义同名运行对象，
分别使用 `mesh`、`texture`、`material`（`asset_ref<const T>`）持有。
各对象只保留对应 `detail::*_resource_access` 一个友元。访问器位于各资源目录的 `detail/`，
提供构造、RID 查询、来源设置、更新序号校验及元信息/材质内容更新，不返回私有字段的可变引用。
只有资源自身实现、资源装配和 Loader 可以包含访问器；普通上层使用只读资源接口。
私有构造由 Access 提供的同步工厂完成，销毁队列仍在原节点内持有对象，保持唯一控制块与无分配入队。

它们与 CPU 负载 `mesh_data`、`texture_data`、`material_data` 是不同类型；
当前 Loader 的混合请求仍返回缓存条目，`entry::as<T>()` 检查类型并取得同一对象的强引用。
缓存清理同时检查条目租约和运行对象强引用，避免对象仍被使用时丢失 URI 身份。
ECS、场景编辑绑定、UI 命令和上层帧快照使用类型化强引用。
`acquire_mesh/material/texture` 返回对应引用；混合请求使用 `render_asset_ref`，在边界用 `asset_cast<T>` 转换。
场景和 Prefab 的绑定记录仍用于作者编辑、预加载及失败恢复；不再额外采集全场景资源保活集合。
资源 `source()` 保存来源域、URI 和资产类型；程序生成对象没有文件来源。
Mesh/Texture 的 `metadata()` 在 CPU 内容采用后可读；未加载时为空，容器纹理未知的 Mip/格式字段也为空。

三类运行对象只能经资源销毁队列构造。最后一个强引用释放只将预分配节点入队，
所属线程在 Application 资产推进、Loader 缓存清理及关闭时消费；不在队列锁内执行析构。
队列先释放 Render RID，再析构 CPU 资源对象；资源析构自身不调用 Render。
材质析构释放纹理强引用产生的后续节点会继续排空。纯 CPU 数据仍使用普通 RAII。
Application 关闭及资源表析构时排空队列并关闭端点、失效全部槽位；关闭后仍存活的外部引用只销毁无服务依赖的外壳，
不会调用旧服务。GPU 对象继续由 Render 执行器管理，CPU 销毁队列不替代 GPU 完成同步。

## 当前帧即时 UI 数据

`application::submit_ui_draw_list` 在 Application 的 `update` 回调内提交后端无关的即时 UI
数据。描述包含显示尺寸、Framebuffer 缩放、RGBA8 顶点、UInt32 索引以及持有 Texture 强引用的绘制
命令；每条命令同时给出裁剪矩形、索引范围和顶点偏移。Runtime 在返回前深拷贝全部数组，调用方可
立即复用或释放源内存。

提交只能发生在 Application 创建线程的 `update` 回调内；同一帧最后一次成功提交原子替换前一份
数据，失败提交保留已有数据。Runtime 会校验有限浮点值、数组范围、索引引用和 Texture
所属关系。数据在该帧渲染或跳过渲染后清空，不跨帧缓存。接口不包含 Dear ImGui 或 Granit 类型，
长期边界见 [ADR-017](../decisions/ADR-017-editor-ui-render-composition.md)。

## 当前帧世界 Debug Draw

`application::submit_debug_draw_list` 在 `update` 回调内提交后端无关的世界线段。每条线包含
两个三维端点、RGBA8 颜色、像素宽度和深度测试开关；Runtime 深拷贝数组，同帧最后一次成功提交
替换前一份数据，失败提交不改变已有内容，帧结束后自动清空。

启用深度测试的线段读取当前 Camera 的深度附件但不写入深度，在场景之后、即时 UI 之前绘制；关闭
深度测试的线段始终可见。接口不包含 Granit 类型，当前只承诺线段，不包含持久图元或世界文字。
设计边界见 [ADR-022](../decisions/ADR-022-debug-draw-boundary.md)。

## ECS 组件

`world::entity_configure_camera` 使用原生 `camera_settings` 设置或替换 Camera 组件。透视参数
必须满足`0 < fov < π`、`near > 0` 且 `far > near`。活动 Camera 通过
`world::set_active_camera` 独立选择，每个 World 至多一个；移除或销毁活动 Camera 后，World
进入无活动 Camera 状态。`world::entity_set_camera` 可同时指定活动相机标志。

帧提取按值复制活动 Camera、世界 Transform、视图矩阵、投影矩阵和视口尺寸。没有关联 Scene Node
的活动 Camera 不进入渲染快照；窗口尺寸为零时跳过渲染。内部矩阵遵循
[ADR-012](../decisions/ADR-012-3d-camera-coordinate-boundary.md)，不进入公共 ABI。

`world::entity_set_mesh_renderer` 设置 Mesh Renderer 组件。组件持有 Mesh 与 Material 强引用，
执行正常的 C++ 构造、复制和析构，不保存 Granit 类型。实体必须关联 Scene Node 才会获得世界 Transform 并
进入渲染快照。资源已销毁或来自其他 Application 时，帧提交返回
`error::invalid_handle`。

## 当前渲染路径

Granit 平台模式将活动 Camera、世界 Transform 与 Mesh Renderer 提取为持有资源的帧包，
后端按 RID 维护 GPU 镜像。带法线 Mesh 使用 Granit 标准 PBR 与索引绘制，材质绑定 base color、
metallic-roughness、normal、occlusion、emissive 五槽和对应因子；未指定槽使用中性默认资源。
切线来自 Mesh，旧网格缺失时保留兼容默认值，不能据此承诺准确法线贴图。
不带法线的旧网格仍使用无光照路径。

base color/emissive 的 sRGB 采样转换为线性空间，其余槽使用线性数据。Cook 资产携带完整 Mip 链，
场景按材质逐槽选择 UV、寻址和过滤，缺省为 repeat/trilinear。CPU 内容采用即普通加载完成；
Render 根据帧中使用的资源独立安排 GPU 上传，帧包固定具体 CPU 内容。负缩放切线空间、UV1、顶点色、MASK 阴影及双面可见性已有 GPU 回归；
BLEND 使用对象级排序和标准透明阶段，不能解决相交三角形或折射，不承诺双面阴影的完整语义。
已接入项与限制见[上游跟踪](../plans/UPSTREAM-043-granit-pbr.md)，不等同于完整 glTF 材质支持。

存在当前帧 UI Draw List 时，Granit 后端在场景 Rendering 结束后通过 Granit Canvas 将 UI 录制到
同一颜色附件，附件使用 `LOAD` 保留场景结果，整帧仍只提交和 Present 一次。Gneiss 在后端内部将
Texture RID 解析为 Texture View，按 Framebuffer Scale 转换顶点和裁剪矩形，并使用 Clamp Sampler；
Canvas 负责动态几何上传、Alpha Pipeline、Scissor 和 UInt32 索引绘制。完全位于视口外的命令会被
跳过，UI 不使用场景深度附件。

资源格式与兼容约束以 [Render 资产格式](render-asset-formats.md) 为准；本页不承诺尚未验收的后端能力。

## 后端材质状态

Render 只维护当前 GPU 内容和必要的在途纹理上传，不提供普通加载用的上传进度接口。
纹理 CPU 负载明确保留，Render 不再同步重读源文件恢复。纹理异步操作持有自己的 CPU 数据，
完成后才采用 GPU 纹理；未完成时场景使用默认纹理，已有纹理则继续使用旧内容。
首次 Mesh 不可用时跳过对象；更新失败保留旧镜像并记录诊断。
材质参数或纹理绑定变化时创建新的后端材质，成功才替换；旧材质保活其实际使用的纹理镜像。
GPU 失败不改变已经成功的 CPU 加载结果。UI 首次纹理未上传完成时跳过对应命令。
本轮未实施 CPU 数据驱逐、Mip/LOD 流送或 GPU 回读恢复。

## 渲染服务对象边界

Application 拥有 `engine::render_service`，用于设备初始化、帧/上传提交、完成回收及关闭。
RID 表位于私有 `resource_storage`，仅保存渲染内容。资源工厂独立持有弱引用身份关联和销毁队列。
Loader 通过工厂更新资源：先提交 Render CPU 内容，成功后更新上层元信息和显式传入的纹理强引用。
Render 不查找或修改上层资源；直接调用 RID 更新接口只改变渲染端数据。
宿主每帧消费工厂销毁队列；关闭时先停止加载和释放场景/缓存，再关闭工厂，最后关闭 Render。
工厂关闭会释放仍被上层引用的 RID 并断开删除器端点，残留引用晚释放只析构 CPU 外壳。
独立使用时 Render 必须覆盖工厂的生命周期；不再由 Render 自动关闭上层资源端点。
Application、Loader、UI 和帧构建使用 `render_service` 的 RID 操作及只读数据查询。
仅构造服务和操作 CPU 资源不会初始化 GPU；无窗口路径调用 shutdown 可关闭资源，重复关闭成功。
`render_service.hpp` 不包含 Granit 头文件，也不暴露 Granit 类型；后端状态放在 `.cpp` 的 PImpl 中。
关闭统计使用引擎值类型 `render_device_stats`，不返回设备或后端句柄。

服务不可复制或移动；GPU 工作仍由原有渲染执行器调度。宿主必须在关闭平台前调用 shutdown，
借用的日志队列应覆盖服务关闭过程。当前仍只有 Granit 实现，PImpl 不代表已经支持运行时切换后端。
