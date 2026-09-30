<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Render 资源、组件与帧提取

## 资源生命周期

Mesh、Material 和 Texture 由 Application 的 Resource Service 独占。`gneiss_mesh_create` 会在调用
期间复制顶点、可选法线、切线、UV1、顶点色及可选 UInt32 索引；调用返回后，调用方可以立即释放源数据。Mesh 至少需要
三个有限值顶点。索引非空时数量必须至少为三个且为三的倍数，所有索引必须小于顶点数；索引为空时
顶点按 Triangle List 顺序解释。法线必须与顶点一一对应、保持有限且归一化；不提供法线的旧 Mesh
使用无光照兼容路径。

切线 XYZ 必须归一化并与对应法线正交，W 为 `+1/-1`，数量必须与顶点一致。旧描述大小由
`GNEISS_MESH_DESC_VERSION_1_SIZE` 标识，按缺省切线处理；Version 2 保留切线、缺省 UV1/顶点色。
新增属性的数量、有限值及缺省规则见[资产格式](render-asset-formats.md)，不读取旧描述尾部。

Material 保存线性 RGBA、金属度、粗糙度、五类 PBR Texture RID 及法线/AO/自发光因子，
字段范围与纹理用途见 [Render 资产格式](render-asset-formats.md#material)。创建不转移 Texture 所有权；
直接使用 C API 时调用方必须维持依赖有效，资产 Loader 的 Material 租约则持有全部 Texture 租约。
`GNEISS_MATERIAL_DESC_VERSION_1_SIZE` 与 Version 2 兼容旧描述，新增字段使用中性默认值。
当前描述支持逐槽 UV0/UV1 与采样器、OPAQUE/MASK/BLEND、Alpha Cutoff 和双面。

`gneiss_texture_create` 当前只接受二维 RGBA8 像素，并显式区分线性与 sRGB 颜色空间。宽高必须位于
`1..16384`，解码后的紧凑像素总量不得超过 256 MiB；行跨度至少为 `width * 4`，输入缓冲区必须覆盖
最后一行。Resource Service 会逐行复制并移除源数据的行尾填充，调用返回后调用方可以释放像素。
Material 可选引用 Texture RID；Granit 后端按 RID 建立 GPU 镜像并通过内置 Shader 采样。

所有 Render RID 只能交还给创建它们的 Application；RID 校验资源类型、generation 和 Service
domain。销毁、跨 Application 使用、类型混用或重复销毁均返回
`GNEISS_ERROR_INVALID_HANDLE`。

## 当前帧即时 UI 数据

`gneiss_application_submit_ui_draw_list` 在 Application 的 `update` 回调内提交后端无关的即时 UI
数据。描述包含显示尺寸、Framebuffer 缩放、RGBA8 顶点、UInt32 索引以及引用 Texture RID 的绘制
命令；每条命令同时给出裁剪矩形、索引范围和顶点偏移。Runtime 在返回前深拷贝全部数组，调用方可
立即复用或释放源内存。

提交只能发生在 Application 创建线程的 `update` 回调内；同一帧最后一次成功提交原子替换前一份
数据，失败提交保留已有数据。Runtime 会校验有限浮点值、数组范围、索引引用、保留字段和 Texture
所属关系。数据在该帧渲染或跳过渲染后清空，不跨帧缓存。接口不包含 Dear ImGui 或 Granit 类型，
长期边界见 [ADR-017](../decisions/ADR-017-editor-ui-render-composition.md)。

## 当前帧世界 Debug Draw

`gneiss_application_submit_debug_draw_list` 在 `update` 回调内提交后端无关的世界线段。每条线包含
两个三维端点、RGBA8 颜色、像素宽度和深度测试开关；Runtime 深拷贝数组，同帧最后一次成功提交
替换前一份数据，失败提交不改变已有内容，帧结束后自动清空。

启用深度测试的线段读取当前 Camera 的深度附件但不写入深度，在场景之后、即时 UI 之前绘制；关闭
深度测试的线段始终可见。接口不包含 Granit 类型，当前只承诺线段，不包含持久图元或世界文字。
设计边界见 [ADR-022](../decisions/ADR-022-debug-draw-boundary.md)。

## ECS 组件

`gneiss_world_entity_configure_camera` 使用带 `struct_size` 的描述设置或替换 Camera 组件。透视参数
必须满足视场角位于 `0..π`、`near_plane > 0` 且 `far_plane > near_plane`。活动 Camera 通过
`gneiss_world_set_active_camera` 独立选择，每个 World 至多一个；移除或销毁活动 Camera 后，World
进入无活动 Camera 状态。`gneiss_world_entity_set_camera` 保留用于兼容旧调用方。

帧提取按值复制活动 Camera、世界 Transform、视图矩阵、投影矩阵和视口尺寸。没有关联 Scene Node
的活动 Camera 不进入渲染快照；窗口尺寸为零时跳过渲染。内部矩阵遵循
[ADR-012](../decisions/ADR-012-3d-camera-coordinate-boundary.md)，不进入公共 ABI。

`gneiss_world_entity_set_mesh_renderer` 设置 Mesh Renderer 组件。组件只借用 Mesh 与 Material RID，
不延长资源生命周期，也不保存 Granit 类型。实体必须关联 Scene Node 才会获得世界 Transform 并
进入渲染快照。资源已销毁或来自其他 Application 时，帧提交返回
`GNEISS_ERROR_INVALID_HANDLE`。

## 当前渲染路径

Granit 平台模式将活动 Camera、世界 Transform 与 Mesh Renderer 提取为持有资源的帧包，
后端按 RID 维护 GPU 镜像。带法线 Mesh 使用 Granit 标准 PBR 与索引绘制，材质绑定 base color、
metallic-roughness、normal、occlusion、emissive 五槽和对应因子；未指定槽使用中性默认资源。
切线来自 Mesh，旧网格缺失时保留兼容默认值，不能据此承诺准确法线贴图。
不带法线的旧网格仍使用无光照路径。

base color/emissive 的 sRGB 采样转换为线性空间，其余槽使用线性数据。Cook 资产携带完整 Mip 链，
场景按材质逐槽选择 UV、寻址和过滤，缺省为 repeat/trilinear。CPU 资产准备及 GPU 上传、候选提交
与旧帧持有由现有资产加载链路管理。负缩放切线空间、UV1、顶点色、MASK 阴影及双面可见性已有 GPU 回归；
BLEND 使用对象级排序和标准透明阶段，不能解决相交三角形或折射，不承诺双面阴影的完整语义。
已接入项与限制见[上游跟踪](../plans/UPSTREAM-043-granit-pbr.md)，不等同于完整 glTF 材质支持。

存在当前帧 UI Draw List 时，Granit 后端在场景 Rendering 结束后通过 Granit Canvas 将 UI 录制到
同一颜色附件，附件使用 `LOAD` 保留场景结果，整帧仍只提交和 Present 一次。Gneiss 在后端内部将
Texture RID 解析为 Texture View，按 Framebuffer Scale 转换顶点和裁剪矩形，并使用 Clamp Sampler；
Canvas 负责动态几何上传、Alpha Pipeline、Scissor 和 UInt32 索引绘制。完全位于视口外的命令会被
跳过，UI 不使用场景深度附件。

资源格式与兼容约束以 [Render 资产格式](render-asset-formats.md) 为准；本页不承诺尚未验收的后端能力。
