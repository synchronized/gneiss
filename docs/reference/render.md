<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Render 资源、组件与帧提取

当前 Service 负责命令接受、背压、执行器和完成分发；Granit 设备实现位于
`backend/granit/backend.hpp/.cpp`，命名空间为 `gneiss::render::backend::granit`。
后端独占设备、交换链、管线及 GPU 资源镜像，在 Service 消费线程串行执行；不拥有另一套队列，
不访问 World、资产管理器或 Service 的 RID 表。后端接口只使用引擎值类型，Granit 类型隐藏在实现中。
Service 外壳仅拥有一个 PImpl。其资源存储与场景存储平级，资源存储不拥有场景；
私有定义集中于 `detail/service_impl.hpp` 和 `detail/resource_storage.hpp`。
资源身份与场景消费状态位于 `gneiss::render::detail`。析构先排空并停止执行器，
再按成员顺序释放后端、场景和资源；已接受任务使用期间不会提前销毁这些状态。
执行器及命令调度类型归入 `gneiss::render::detail`，定义在 `detail/executor.hpp/.cpp`。
截图通过 `capture_request.hpp` 返回独立请求，图像值定义在 `frame_result.hpp`；调用方不包含执行器头；
普通模块的内部头文件集合也不再导出执行器。
设备计数和单帧执行指标定义于 `statistics.hpp`，不再要求后端依赖 Service 或执行器声明。

Render 的 `reserve_mesh/texture/material()` 与 `create_*` 系列返回 `result<rid>`；
失败结果不含 RID，调用方无需准备输出指针。销毁仍返回 `error`。
`prepare_mesh/material/texture_data()` 返回 `result<shared_ptr<const ..._data>>`，
只准备和登记 CPU 数据，不等待 GPU 上传。资源服务内部创建链同样使用值返回。

普通帧使用 `render(frame_request&&)`，截图使用 `request_capture(frame_request&&, capture_size)`；
宿主不再准备或复用 packet。截图返回 `result<capture_request>`，接受不等待 GPU，也不暴露执行器序号。
内部 packet 提交和执行器的 `submit_frame/submit_command` 仍使用序号关联完成结果。
底层 `rid_table::create(type, value)` 返回 `result<Identifier>`，句柄的 generation、类型与所属域校验不变。
关闭流程仍通过 error 和统计参数同时报告错误及资源统计，失败时统计信息也必须保留。

## 资源生命周期

Mesh、Material 和 Texture 的 CPU 内容表由 Application 的 `render::service` 私有实现拥有。
调用方通过 `application::render()` 使用 RID 接口，不能借用资源表。
运行身份、元信息与材质依赖由 Application 的 `asset_manager` 维护，通过 `application::assets()` 借用。
程序化纹理使用 `assets.create(texture_view)`，返回 `result<ref<const texture>>`；网格与材质使用对应的创建接口。
资产管理器的 `create(mesh_view/texture_view)` 创建有内容的资源，不公开匿名预留入口；
加载过程中的身份预留由内部构建器完成，Render 的 RID 预留接口继续保留。
`create(parameters, textures)` 显式传入纹理强引用。成功输出拥有者，失败输出为空。
不存在外部 RID 接管入口；不同管理器不能接管彼此的资源。
管理器不提供 RID 到资产对象的反查；上层直接保存创建或加载得到的强引用。
普通文件加载通过同一个管理器的 URI 缓存复用对象。上层提取的 RID 仅用于借用，不得手动销毁资源拥有的 RID。
底层 Render 的裸 RID 创建/销毁接口保留，调用方须自行配对，不能混入强引用所有权。
ImGui 适配器直接把自身持有的字体纹理引用写入 UI 绘制命令；当前只登记字体图集，未知纹理 ID 返回 unsupported。
管理器位于 `engine/function/asset/asset_manager.hpp`；批次加载状态、资源工厂及销毁队列属于其私有实现；更新任务不属于对外接口。
宿主先停止资产加载并销毁管理器，再关闭 Render；资源对象头不声明 Render 服务类型。
原生 `render::service::create_mesh(mesh_view, ...)` 会在调用
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

`engine/resource` 顶层的 `mesh.hpp`、`texture.hpp`、`material.hpp` 分别定义同名运行对象，
上层使用 `ref<const mesh>`、`ref<const texture>`、`ref<const material>` 持有。
各对象只保留对应 `detail::*_resource_access` 一个友元。访问器位于各资源目录的 `detail/`，
提供构造、RID 查询、来源设置、更新序号校验及元信息/材质内容更新，不返回私有字段的可变引用。
只有资源自身实现、资源装配和 Loader 可以包含访问器；普通上层使用只读资源接口。
私有构造由 Access 提供的同步工厂完成；CPU 对象使用独立控制块，预分配队列节点只保存 RID。

它们与 CPU 负载 `mesh_data`、`texture_data`、`material_data` 是不同类型；
混合请求返回 `render_resource_ref`，`as<T>()` 检查类型并取得同一对象的强引用。
缓存直接弱持有实际对象；清理只删除无在途操作且对象已释放的索引。
ECS、场景编辑绑定、UI 命令和上层帧快照使用类型化强引用。
`acquire_mesh/material/texture` 返回对应引用；混合请求使用 `render_resource_ref`，在边界用 `resource_cast<T>` 转换。
场景和 Prefab 的绑定记录仍用于作者编辑、预加载及失败恢复；不再额外采集全场景资源保活集合。
资源 `source()` 保存来源域、URI 和资产类型；程序生成对象没有文件来源。
Mesh/Texture 的 `metadata()` 在 CPU 内容采用后可读；未加载时为空，容器纹理未知的 Mip/格式字段也为空。

三类资产通过内部工厂构造并拥有后端释放令牌。最后一个强引用释放时，CPU 对象、内容及
依赖正常析构；令牌将预分配 RID 节点入队，所属线程在资产推进、回收和关闭时消费。
材质析构立即释放纹理引用，不必等队列处理 CPU 对象。队列锁内不调用 Render。
关闭时排空队列并失效所有 RID；关闭后外部 CPU 引用及固定快照仍可读取，晚释放不访问旧服务。
GPU 在途安全回收仍由 Render 及后端负责，RID 释放队列不替代 GPU 完成同步。

## 当前帧即时 UI 数据

宿主持有 `ui_draw_list`，通过 `replace(ui_draw_view, renderer)` 复制后端无关的即时 UI
数据，再将列表装入 `frame_request::ui`。描述包含显示尺寸、Framebuffer 缩放、RGBA8 顶点、
UInt32 索引和持有 Texture 强引用的绘制命令；调用方可在复制成功后复用源数组。
校验包含有限浮点值、索引范围和纹理所属关系；失败保留原列表。宿主负责替换或清空数据，
Application 不保存 UI 状态。接口不包含 Dear ImGui 或 Granit 类型。
长期边界见 [ADR-017](../decisions/ADR-017-editor-ui-render-composition.md)。

## 当前帧世界 Debug Draw

宿主持有 `debug_draw_list`，通过 `replace(lines)` 复制线段，再装入 `frame_request::debug`。
每条线包含两个三维端点、RGBA8 颜色、像素宽度和深度测试开关；失败保留原列表。
启用深度测试的线段读取相机深度附件但不写入深度，在场景之后、即时 UI 之前绘制；关闭
深度测试的线段始终可见。列表不自动按 Application 帧清空，宿主负责本帧输入的生命周期。
设计边界见 [ADR-022](../decisions/ADR-022-debug-draw-boundary.md)。

## ECS 组件

通过 `object.add_component<camera_component>(settings, primary_hint)` 新增。
`settings` 是公开数据，赋值不会自动校验或回滚；调用方应先检查 `settings.valid()`，
再赋值并调用 `object.notify_component_modified<camera_component>()`。透视参数必须为有限值，
满足 `0 < fov < π`、`near_plane > 0` 且 `far_plane > near_plane`。
投影计算入口仍拒绝非法参数，且失败时不修改输出矩阵。活动 Camera 通过
`set_active_camera(world&, entity_id)` 独立选择，每个 World 至多一个；移除或销毁活动 Camera 后，相机选择查询
进入无活动 Camera 状态。primary_hint 仅是默认相机选择提示，安装组件不会激活相机。

相机查询入口为 `function/scene/rendering/render_camera.hpp` 中的 `select_render_camera`。
它在 World 所属线程读取活动相机或调用期间借用的视口相机，返回可选的相机纯值数据；
没有活动相机时返回空值，不枚举网格对象。矩阵遵循
[ADR-012](../decisions/ADR-012-3d-camera-coordinate-boundary.md)。相机值类型位于 Render 的 `camera.hpp`。

使用 add_component<mesh_component>(mesh_component{mesh, material}) 新增网格渲染组件；已有组件通过借用指针修改后，
调用 notify_component_modified<mesh_component>() 通知绑定。组件持有 Mesh 与 Material 强引用，
直接通过组件的 mesh/material 字段访问，执行正常的 C++ 构造、复制和析构，不保存 Granit 类型。
持久化描述使用 mesh_component_description，保持原有类型 GUID、属性 ID 及 mesh/material 文件字段。World 侧绑定将变化同步至持久渲染场景，
帧捕获固定资源内容版本；无资源的对象暂不创建实例，非法资源 RID 在提交时拒绝。

## 当前渲染路径

Granit 平台模式由 World 绑定增量同步实例，Render 从持久场景捕获帧数据并固定资源版本，
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

Application 拥有 `gneiss::render::service`，用于设备初始化、帧/上传提交、完成回收及关闭。
RID 表位于私有 `resource_storage`，仅保存渲染内容。资源工厂独立持有弱引用身份关联和销毁队列。
Loader 通过工厂更新资源：先提交 Render CPU 内容，成功后更新上层元信息和显式传入的纹理强引用。
Render 不查找或修改上层资源；直接调用 RID 更新接口只改变渲染端数据。
宿主每帧消费工厂销毁队列；关闭时先停止加载和释放场景/缓存，再关闭工厂，最后关闭 Render。
工厂关闭会释放仍被上层引用的 RID 并断开删除器端点，残留引用晚释放只析构 CPU 外壳。
独立使用时 Render 必须覆盖工厂的生命周期；不再由 Render 自动关闭上层资源端点。
Application、Loader、UI 和帧构建使用 `render::service` 的 RID 操作及只读数据查询。
仅构造服务和操作 CPU 资源不会初始化 GPU；无窗口路径调用 shutdown 可关闭资源，重复关闭成功。
shutdown 排空旧工作和完成回执后关闭设备；即使报告历史执行错误，CPU 场景和资源入口也会关闭，旧 RID 失效。
`service.hpp` 不包含 Granit 头文件，也不暴露 Granit 类型；后端状态放在 `.cpp` 的 PImpl 中。
关闭统计使用引擎值类型 `render::device_stats`，不返回设备或后端句柄。

服务不可复制或移动；GPU 工作仍由原有渲染执行器调度。宿主必须在关闭平台前调用 shutdown，
借用的日志队列应覆盖服务关闭过程。当前仍只有 Granit 实现，PImpl 不代表已经支持运行时切换后端。

## 完成结果领取

执行器的 `try_take_frame_completion()`、`try_take_command_completion()` 返回 `std::optional`，
空值表示尚无可领取记录；领取成功移出队列。执行失败仍返回包含序号、统计和 `status` 的记录。
`query_command_status()` 返回可选的状态快照，不消费完成记录。
服务内部收集帧/命令错误；截图请求由执行线程直接发布终态，不需要主线程另行分发回执。
`capture_request::ready()` 判断终态，`take()` 领取自有图像；未完成返回 not_ready，
成功图像只能领取一次，后续返回 invalid_state。空请求不 ready，take 返回 invalid_state。
`cancel()` 只对未完成请求生效，此后 take 返回 cancelled；它不强行中断 GPU 工作。
请求副本共享同一结果和领取状态，服务关闭后仍可使用；丢弃请求不会中断已接受的工作。

## 持久场景更新与单窗口绘制

`render::service::create_scene()` 创建独立 CPU 渲染场景，无需先初始化窗口或 GPU。
`submit(render::scene_commands&&)` 接受自有命令列表，支持创建、变换、网格/材质换绑、
层掩码/显隐/阴影标记和删除。成功清空列表并将其 scene 置零，返回失败时列表与场景均不变。
`destroy_scene(scene)` 立即使场景 RID 失效，不受命令队列容量限制；不影响其他场景。
已接受的命令和帧任务保活原场景内容，最后一个引用释放时回收实例及资源引用。以上接口均在服务所属线程调用。

最小调用示例（所有操作均在服务所属线程）：

```cpp
auto scene = renderer.create_scene();
if (!scene)
  return scene.error();
render::scene_commands commands{
    *scene, {render::create_instance{{1}, {.mesh = mesh_rid, .material = material_rid}}}};
auto accepted = renderer.submit(std::move(commands));
// 返回失败时列表和场景均不改变；成功后不要再次发送。
// 使用结束时调用 renderer.destroy_scene(*scene)。
```

实例由 `{scene RID, render::instance_key}` 定位。key 是生产者分配的非零 64 位值，
每个场景的创建命令按 key 递增提交，删除后不复用；不同场景允许使用相同 key。
场景 RID 校验所属服务和 generation，旧场景的命令不会作用于复用槽位的新场景。
同列表可以先创建再更新/删除；重复删除、访问已删除 key、零 key 和重用旧 key 均拒绝。

命令列表没有服务指针、预留资源或析构回调，允许晚于服务销毁；提交前须由生产者保活资源。
校验时只暂存被触及实例，不复制全场景；全部通过后转移存储节点，发布阶段不分配实例存储。
返回失败不会部分应用，关闭服务后提交返回 invalid_state。内存耗尽遵循项目致命错误策略，
不作为可重试结果。

变换保存完整仿射矩阵，允许剪切；非有限、非仿射或奇异矩阵返回 invalid_argument。
创建/资源换绑接受已预留但尚未就绪的网格、材质；未就绪本身不再导致提交失败。
实例保活内部资源身份，replace_* 返回 result<void>：接受后所属线程可读取新内容，
消费者按队列顺序发布版本，后续帧捕获读取已消费版本，无需重建实例。
队列满返回 not_ready，旧内容不变；资产加载保留已解码内容，在下一次 update 重试采用。
材质身份保活各槽位的纹理身份，纹理独立更新不要求重新发布材质。
释放外部 RID 后不能新增引用或继续通过该 RID 发布，但已有实例/材质依赖仍存活。
材质更新可以保留或重排自身已持有的纹理身份；新增无效依赖会拒绝更新，原内容和依赖不变。

内部 scene_resource_pin::capture() 固定当前不可变 payload，捕获后的内容不受热重载、换绑或
场景卸载影响。空 payload 表示尚未就绪，不表示无效身份。身份通过原子共享指针发布不可变版本，材质内容及纹理槽位绑定整体发布，持有身份的消费者可安全读取版本。
RID 表和 Service 接口仍限所属线程。初始化后，场景命令、资源版本发布及帧捕获共用有序执行器；
捕获只读取已消费版本，后续提交不会提前影响较早的帧。未初始化执行器时，CPU 操作同步生效。

`render::service::render(render::frame_request&&)` 接受多个离屏视图和最多一个窗口视图，视图给出 scene RID、view/projection
矩阵、相机位置和层掩码。它将自有请求排入执行器，到达队列位置后才捕获场景及资源版本并交给 Granit；
接受成功消费请求，接受失败保留请求；异步准备失败通过帧完成回执报告，不调用 GPU 绘制。
零视图仅绘制 UI，不绘制场景。未初始化设备时不能提交绘制，接受成功不代表 GPU 完成。

帧内实例只包含 RID、完整世界矩阵/法线矩阵和层掩码，不持有上层 Mesh/Material 对象。
捕获时过滤隐藏或层不匹配实例；未就绪网格由绘制消费者跳过，材质/纹理沿用缺省内容策略。
剪切通过完整矩阵保留，包围球按变换后的顶点计算。旧 World 帧快照在入口转换为同一绘制表示。

**当前限制**：新入口支持多个独立离屏目标和最多一个窗口视图；重复目标返回 invalid_argument。casts_shadows=false 映射为 Granit 的关闭投射标志，主视图仍正常绘制。Granit 0.73.2 的标准 PBR 材质已采样方向光阴影贴图，关闭投射会消除接收面上的对应阴影。
窗口输入暂未替换为 target RID。新入口携带自有 UI/调试数据并固定 UI 纹理版本，
UI 纹理尚未就绪返回 not_ready。Runtime/Editor 共用 Application 的普通帧已改用 World 增量绑定；
截图也使用 frame_request，执行策略为必达帧。当前实现仍逐帧捕获持久场景；后端拆分、目标视图及 GPU 验收按
[PLAN-115](../plans/PLAN-115-persistent-render-scene.md) 继续实施。

## 内部场景对象

Service 的私有实现直接持有场景 RID 表。每个槽位保存提交侧已接受身份与消费侧
render::scene 引用；Scene 自己保存实例变换、可见性及资源身份，不访问 World 或执行器。

提交时先校验整批命令并固定资源引用，队列接受成功后才更新身份记录。
消费侧依次应用命令、按视图提取帧数据，再交由后端绘制。
销毁场景立即使 RID 失效，在途任务仍保活 Scene；内部共享引用不表示 Scene 可并发修改。
当前提取仍遍历实例并过滤可见性/层掩码，不新增空间索引或视锥剔除能力。

## World 增量绑定

`world_render_binding::create(world, render::service)` 创建绑定及其拥有的 Render 场景。
两个对象均须覆盖绑定使用期；创建、同步和销毁限所属线程。`scene()` 返回借用 RID，
调用方不能自行销毁该场景。销毁绑定释放渲染场景，不销毁 World 对象。

```cpp
// owner 和 renderer 已由宿主创建；owner 可包含多个场景实例。
auto created = world_render_binding::create(owner, renderer);
if (!created)
  return created.error();
auto binding = std::move(*created);

// 逻辑更新结束后调用。借用组件修改后需 notify_component_modified<T>()。
owner.flush_changes(); // 所属线程调用；所有消费者共享一次合并后的通知。
auto synced = binding->synchronize();
if (!synced)
  return synced.error(); // 状态仍在绑定中，下次可重试。

// 帧视图引用 binding->scene()，再通过 renderer.render(frame_request) 提交。
// 关闭时先销毁绑定，再关闭 renderer 和 owner。
binding.reset();
```

创建绑定时枚举已有网格对象，后续订阅 world.changes()，只处理变化。
World 合并重复修改，在宿主调用 flush_changes() 时广播借用批次；回调只能记录待办，不能修改 World。
该调用返回 void，所属线程是调用前置条件，违例在 Debug/Release 均断言终止；回调异常沿原有异常边界处理，不能当作可重试广播。
消费者分别保存自己的待办，绑定同步不触发广播；没有订阅时 World 不积累通知。
父变换、改父节点、网格组件增删/换绑、
对象删除和场景卸载均生成相应命令。矩阵使用完整 world_matrix，保留层级剪切。
组件缺少 mesh/material 引用时暂不创建实例；拥有尚未发布内容的有效资源身份仍可创建，
内容随后通过 Render 资源身份发布。一个绑定覆盖一个 World 的所有可渲染对象。

提交前 World 的组件引用保活资源，接受后 Render 实例保活资源身份。提交失败不更新绑定映射、
不清空变化，下次从 World 当前状态重新生成命令；创建后未同步就删除不会留下实例。
多个绑定独立消费；静态 World 不逐帧枚举全部对象。当前不提供 World 侧显隐/层掩码组件。

Application 在逻辑更新回调结束后统一发布 World 变化；截图入口也先发布。
有窗口时创建绑定，普通帧依次同步变化、查询所选相机、提交 frame_request。
没有相机仍可显示 UI；最小化时仍同步对象变化。无窗口运行不创建绑定。
关闭先销毁绑定，再释放 World 和 Render。背压与完成回执由 Render 内部处理，
普通帧 not_ready 时宿主保留窗口重建标记，稍后以最新输入再提交。
完成回执消费后直接释放 packet，不再维护无消费者的容量回收缓存。

宿主截图和普通帧可共用绑定同步、相机查询及 frame_request 构建。
`request_capture` 失败保留帧输入，成功返回独立请求；必达帧不能被普通帧替换。
测试同步辅助用 finish_frames 等待后调用请求的 take；生产调用方逐帧查询 ready，不等待 GPU。
shutdown 排空已接受工作并发布终态，历史执行错误不会阻止资源关闭。
capture_request 支持取消；取消只放弃结果，不中断已提交 GPU 工作，副本共享一次成功领取。

旧 render_snapshot、对象提取与独立捕获入口已删除；Render 内部仍逐帧捕获持久场景。
GPU 图像验收不表示已删除内部逐帧场景捕获。

帧完成回执仅保存序号、状态与执行统计，不返回整份帧包。执行结束或排队帧被替换时释放其 CPU 内容引用，
不依赖主线程是否领取完成记录；GPU 在途资源回收仍由后端管理。

持久实例在创建或变换命令接受时计算并缓存法线矩阵。非仿射、不可逆或求逆产生非有限值的矩阵
返回 invalid_argument，保持原实例及调用方命令不变；帧捕获复用缓存，不逐帧求逆。

内部场景核心将接受记录（有效实例 key、递增序号）与消费内容（实例描述、矩阵、资源依赖）分开。
命令先校验并取得资源依赖，再交给可靠、有序且仅执行一次的调度回调；拒绝时保留原命令和接受记录。
帧捕获任务绑定场景内容，不访问接受侧 RID 表。生产 Service 已将命令、资源发布与帧准备接入同一执行器。
关闭设备时停止提交并排空任务；CPU-only 路径无需创建执行线程或初始化 GPU。

帧消费数据位于 `render::detail::frame_packet`，定义在 `detail/frame_packet.hpp`。
资源快照及绘制实例列表只供 Service、执行器和后端消费，不属于宿主调用接口或普通模块共享头文件。

## 队列容量

`service::initialize` 最后一个参数 `render::queue_options` 在初始化时固定容量。
`pending_frames` 默认 3，限制等待消费的帧，不含正在绘制的帧；
`commands` 默认 12，限制所有已接受但未完成的命令，包含正在执行及可恢复重排的命令。
两者必须非零，否则返回 invalid_argument。命令满时返回 not_ready 且不执行；完成后可重试。
命令成功或失败到达终态后，执行线程先在队列锁外释放回调捕获数据，再发布完成回执和归还容量。
可恢复命令的中间 not_ready 继续保留数据和名额；执行器 flush 等待上述 CPU 释放，不代表 GPU 已空闲。
帧和命令独立计数，命令饱和不会占用帧名额；必达帧不替换，普通帧沿用替换策略。
被替换帧的捕获数据在提交线程、队列锁外释放，之后才发布 dropped 回执并通知完成。
执行器 flush 同时等待这部分回收；回执按 sequence 关联，不依赖丢帧回执与执行回执的到达顺序。
`command_payload_bytes` 可选限制在途命令直接携带的数据，零表示不限制；默认暂不启用字节上限。
同一不可变共享所有者在全部关联命令终结前只计费一次；命令独占数组分别计费。
暂时积压超额返回 not_ready；单份数据超过整个预算返回 out_of_memory，不要求永久重试。
拒绝不改变已接受的场景或资源版本；零数据释放命令不消耗字节额度，仍受命令数量限制。
queue_stats 的 command_payload_bytes 与 command_payload_high_watermark 分别报告当前和峰值占用。

计费包含 Mesh/Texture 发布数据、Material 发布版本及直接内容、场景命令数组容量和环境字节数据。
这是逻辑 payload 预算，不递归计入资源引用图，不计算分配器开销、常驻资源、捕获帧或 GPU 内存。
初始化同步借用的环境数据不进入异步积压计费。无执行器的同步路径不形成队列，也不应用该预算。
`frame_input_bytes` 独立限制已接受帧的视图、UI 顶点/索引/命令和调试线段输入总字节数，
默认零表示不限制。等待、执行和替换回收中的输入均计费，释放后才归还额度。
统计通过 frame_input_bytes / frame_input_high_watermark 查询。
单帧超过整个预算返回 out_of_memory；临时满额返回 not_ready，拒绝保留调用方请求和原等待帧。
替换不提前扣除旧帧额度，新旧输入共存时也必须满足上限。
这不是总内存硬上限：不含调用方原输入、提交时临时副本、分配器开销、场景常驻数据、
渲染线程执行时生成的绘制快照和 GPU 数据。
Service 普通交互帧仍采用最多一个等待帧的低延迟策略。

## 离屏目标

`create_target({width, height})` 创建固定 RGBA8 目标身份，尺寸必须为 1–16384。
GPU 表示在首次绘制时创建并复用。`view.target` 为零表示窗口，非零表示本服务的离屏目标；
离屏输出采用目标尺寸，不触发窗口获取或呈现。设备仍须先通过 initialize 初始化。
同帧支持多个视图，每个目标最多出现一次，窗口最多一个；改变尺寸需要创建新目标。

`request_capture` 可以回读指定目标；接受成功后通过返回请求的 take 取得图像。
`destroy_target` 按序释放后端表示，接受成功使 RID 失效；not_ready 表示未接受，须保留句柄重试。
已接受的旧帧在释放命令前完成消费，后端继续负责 GPU 退休。服务关闭统一释放目标。
该接口不是可忽略错误的析构入口。正常绘制与目标替换已通过 Windows 神庙双视图 GPU 验收，压力与设备丢失仍待验证。

同帧视图共享一份去重资源快照，消费端统一准备资源，先绘制离屏目标再呈现窗口。帧级 UI 只应用于窗口输出，调试输入应用于每个视图。单视图截图返回该视图；多视图截图返回窗口 UI 合成结果，没有窗口输出时返回 unsupported。执行失败不会撤销已经完成的离屏输出。GPU 计时沿用最近有效的窗口样本，不表示所有输出耗时之和。此链路已通过 CPU 捕获测试与 Windows 神庙双视图 GPU 合成回读测试。

### 在 UI 中显示离屏目标

`ui_draw_command::texture` 使用 `ui_image`，可保存纹理资产强引用或 `ui_target_image{target}`。
后者借用本服务目标 RID，不伪造 CPU 纹理资产。ImGui 适配器将字体以外的 TexID 作为目标 RID，
由 Render 检查类型、所属服务和存活状态。

目标图像必须在同一 `frame_request.views` 中有生产视图，并存在窗口视图作为 UI 消费者；
缺少生产者/窗口返回 `invalid_argument`，失效或跨服务目标返回 `invalid_handle`，请求不被消费。
帧接受后通过有序执行保证先绘制目标、再由窗口 UI 采样，后续目标销毁排在旧帧之后。
UI 不绘制到离屏目标，避免目标反馈读取；单纯的离屏截图也不包含帧级 UI。
改变尺寸应创建新目标并更新后续 UI/视图，再按销毁接口处理旧目标。

```cpp
command.texture = ui_target_image{target};
// draw_list.replace(...) 成功后装入 request.ui。
request.views = {{.scene = scene, .target = target}, {.scene = scene}};
auto submitted = renderer.render(std::move(request));
// not_ready 保留请求供重试；target 仍由调用方负责销毁。
```

编辑器 Scene View 已通过 ImGui Image 使用该路径，投影、网格与 Gizmo 使用图像尺寸；GPU 显示待验收。


宿主直接构造 `frame_request::views`，每项包含渲染场景、目标和相机矩阵；同一 World 的
渲染场景可由多个视图共享，并按各目标尺寸计算投影。Render 校验目标归属和重复输出。
宿主显式添加窗口 UI 视图，不存在 Application 自动追加视图或默认相机的规则。
调用方负责目标生命周期，停止向新帧引用目标后再请求销毁。
查看器双视图用例见[示例说明](../../examples/README.md)。
`view.scene == 0` 表示空输出，不携带场景相机和世界空间调试线。
编辑器按 Scene View 内容区域与 framebuffer scale 创建目标；尺寸变化时替换，
最多保留一个待退休目标。背压期间按旧目标比例显示，不阻塞等待；退出时随 Render 关闭释放。

### 执行结果与内部回执契约

Service 的初始化、关闭在所属线程串行调用。初始化使用新执行器；关闭先排空历史工作与回执。
这两条命令提交成功且 flush 返回后，必须存在对应序号的完成回执；缺失或错号触发内部契约断言。
初始化结果直接取自回执，不再由回调另外写一份共享结果变量。

回执中的失败状态仍正常返回，包括后端 internal、显式预算 out_of_memory 和暂时 not_ready。
关闭仍尽力完成设备关闭并优先报告历史执行错误；flush 只等待 CPU 执行与捕获引用释放，
不保证 GPU 空闲。第三方读回格式异常仍是后端诊断，不按内部回执不变量处理。


## 环境包提交

`request_environment(bytes, intensity, rotation)` 接受共享不可变二进制包；空 shared_ptr 表示
显式选择内建环境，非空指针不能指向空包。强度必须有限且非负，旋转必须有限。
成功只表示命令已入队，后台执行失败通过既有完成收集报告；not_ready 表示背压，调用者保留
输入并重试。Render 不读取工程 URI，不持有宿主加载请求。环境包的加载归属见
[资产环境包](assets.md#环境包)。

## 协议类型归属

`render::mesh_instance_description` 是场景命令的实例描述，只属于 Render。
`material_data` 仍是资产装配与渲染资源发布共用的 CPU 材质负载，与 mesh_data、texture_data
保持同一命名层级；它不是可渲染实例或 GPU 对象，不为收敛名称再建立一套副本。
未使用的诊断阶段 render_snapshot 已删除；现有 frame_capture 保留，未恢复旧 World 全量快照接口。

## 独立像素拾取

`service::request_pick(view, size, x, y)` 返回独立 pick_request。
size 和坐标使用物理像素，原点为左上角；零尺寸或越界返回 invalid_argument，
队列背压返回 not_ready。请求按指定相机、层和尺寸重新渲染临时离屏附件，
不读取上一显示帧，也不读写 view.target。

pick_request 的 ready/cancel/take 与截图请求具有相同生命周期：
取消只放弃结果；成功结果只能领取一次；空请求或重复领取返回 invalid_state。
关闭排空已接受工作，请求及结果可在服务关闭后存活。

pick_result 保存 scene RID 与完整的 64 位 instance_key；零 key 表示成功未命中。
GPU 的 32 位序号只用该次绘制快照解释，不使用最新对象列表。
World 绑定的 resolve(hit) 返回仍存活且属于该绑定的 game_object；
对象已删除、组件重建后同步或场景不匹配返回空句柄。
消费方仍需丢弃文档切换或视口变化导致的过期点击。

底层使用 Granit 0.74.1 的 R32_UINT 附件和单像素异步回读。
当前渲染工作线程等待回读终态，主线程不等待；尚未实现多请求在途回读调度。
只支持 Opaque/MASK 拾取，BLEND/UI/Debug 不参与；Editor 点击入口尚未接通。
基础命中、背景、身份替换、尺寸变化及首次关闭资源归零已通过真实 GPU 验收，
完整 MASK/遮挡与取消/失败矩阵仍由 PLAN-115 跟踪。
