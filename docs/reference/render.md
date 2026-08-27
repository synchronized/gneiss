<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Render 资源、组件与帧提取

## 资源生命周期

Mesh、Material 和 Texture 由 Application 的 Resource Service 独占。`gneiss_mesh_create` 会在调用
期间复制顶点数组；调用返回后，调用方可以立即释放源数据。首版 Mesh 只包含三维位置，至少需要三个有限值
顶点；顶点按 Triangle List 解释。

Material 当前只包含线性空间的固定 RGBA 颜色，各分量必须位于 `0..1`。

`gneiss_texture_create` 当前只接受二维 RGBA8 像素，并显式区分线性与 sRGB 颜色空间。宽高必须位于
`1..16384`，解码后的紧凑像素总量不得超过 256 MiB；行跨度至少为 `width * 4`，输入缓冲区必须覆盖
最后一行。Resource Service 会逐行复制并移除源数据的行尾填充，调用返回后调用方可以释放像素。
Material 可选引用 Texture RID；Granit 后端按 RID 建立 GPU 镜像并通过内置 Shader 采样。

所有 Render RID 只能交还给创建它们的 Application；RID 校验资源类型、generation 和 Service
domain。销毁、跨 Application 使用、类型混用或重复销毁均返回
`GNEISS_ERROR_INVALID_HANDLE`。

## ECS 组件

`gneiss_world_entity_set_camera` 设置或替换 Camera 组件。透视参数必须满足视场角位于 `0..π`、
`near_plane > 0` 且 `far_plane > near_plane`。设置新的 primary Camera 时，同一 World 中原 primary
Camera 会被取消；没有关联 Scene Node 的 Camera 不进入渲染快照。

`gneiss_world_entity_set_mesh_renderer` 设置 Mesh Renderer 组件。组件只借用 Mesh 与 Material RID，
不延长资源生命周期，也不保存 Granit 类型。实体必须关联 Scene Node 才会获得世界 Transform 并
进入渲染快照。资源已销毁或来自其他 Application 时，帧提交返回
`GNEISS_ERROR_INVALID_HANDLE`。

## 当前渲染路径

Granit 平台模式在每次 `update` 后提取 primary Camera、Scene Transform 和 Mesh Renderer。CPU
生成本帧的位置、颜色与 UV 顶点，Render Service 上传临时 Vertex Buffer，并按 Material 绑定
base-color Texture 后通过内置 Shader 绘制 Triangle List。无纹理 Material 使用默认白纹理；后端按
Texture RID 缓存 GPU 镜像。按帧 Buffer 保留三个槽位，避免覆盖仍在飞行中的提交。
当前尚无深度缓冲；实例按世界 Transform 的 Z 值从远到近稳定绘制，为 2.5D 示例提供显式层级。

当前路径用于验证 Application、World、Resource Service 与 Granit 的端到端边界，不是正式资产或
渲染管线。暂不支持索引、Mip、光照、深度、剔除和异步上传。
