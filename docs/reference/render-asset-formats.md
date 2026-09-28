<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Render 资产格式

Runtime Mesh 优先使用二进制；旧 Mesh、Material 与 Texture 描述使用严格 UTF-8 JSON。所有格式仅由
内部 Loader 使用，资源 URI 规则见[资产 URI、目录挂载与缓存](assets.md)。

## Mesh Binary v1

建议扩展名为 `.gneiss-mesh`。多字节整数与 IEEE 754 Float32 固定使用小端序，所有 Offset 相对
文件起点。Header 固定为 80 字节：

| Offset | 大小 | 字段 | v1 值或含义 |
| ---: | ---: | --- | --- |
| 0 | 4 | magic | ASCII `GNMS` |
| 4 | 2 | version | `1` |
| 6 | 2 | header_size | `80` |
| 8 | 4 | flags | `0` |
| 12 | 4 | vertex_count | 唯一顶点数 |
| 16 | 4 | index_count | UInt32 索引数，必须是 3 的倍数 |
| 20 | 2 | vertex_stride | `32` |
| 22 | 2 | index_size | `4` |
| 24 | 8 | vertex_offset | `80` |
| 32 | 8 | index_offset | 16 字节对齐 |
| 40 | 8 | file_size | 必须等于实际文件大小 |
| 48 | 24 | bounds | AABB min Float3、max Float3 |
| 72 | 8 | reserved | `0` |

顶点按 Position Float3、UV Float2、Normal Float3 交错排列，共 32 字节。随后填充到 16 字节边界，
再保存 UInt32 索引。v1 只支持三角形列表、有限 Float32 和单位法线；Decoder 必须在分配或读取前验证
版本、数量乘法、Offset、区域重叠、文件边界、AABB 和索引范围。Runtime Loader 将唯一顶点与索引
直接交给 Render Service；Granit 后端通过 Index Buffer 绘制，不再展开为重复顶点。

`gneiss_assetc inspect` 输出摘要，`validate` 只校验，`dump <file> --format json` 按需生成完整 Debug
JSON。Debug JSON 的格式标识为 `gneiss.mesh.debug`，不是 Runtime 输入。

## 兼容 JSON Mesh

建议扩展名为 `.mesh.json`：

```json
{
  "format": "gneiss.mesh",
  "version": 1,
  "topology": "triangle_list",
  "vertices": [
    [-0.5, -0.5, 0.0],
    [0.5, -0.5, 0.0],
    [0.0, 0.5, 0.0]
  ]
}
```

`vertices` 每项为 `(x, y, z)` 位置。顶点数至少为 3 且是 3 的倍数，所有数值必须有限并可表示为
float。v1 只支持 `triangle_list`，不包含索引、法线、UV、颜色或骨骼。

Mesh v2 保留 `vertices`，并增加数量必须与顶点一致的 `uvs`；每项是有限二维纹理坐标 `(u, v)`：

```json
{
  "format": "gneiss.mesh",
  "version": 2,
  "topology": "triangle_list",
  "vertices": [[-0.5, -0.5, 0], [0.5, -0.5, 0], [0, 0.5, 0]],
  "uvs": [[0, 0], [1, 0], [0.5, 1]]
}
```

UV 的两个分量必须为有限 float，允许负数和大于 `1` 的平铺坐标，不进行截断或取模。
当前 Mesh 材质采样使用重复寻址。

Mesh v3 继续要求 `uvs`，并增加数量与顶点一致的单位 `normals`；每项是右手坐标中的 `(x, y, z)`：

```json
{
  "format": "gneiss.mesh",
  "version": 3,
  "topology": "triangle_list",
  "vertices": [[-0.5, -0.5, 0], [0.5, -0.5, 0], [0, 0.5, 0]],
  "uvs": [[0, 0], [1, 0], [0.5, 1]],
  "normals": [[0, 0, 1], [0, 0, 1], [0, 0, 1]]
}
```

法线长度允许 `1e-4` 误差。v1/v2 不隐式生成法线，进入明确的无光照兼容路径。

## Material

建议扩展名为 `.material.json`：

```json
{
  "format": "gneiss.material",
  "version": 1,
  "color": [0.95, 0.35, 0.12, 1.0]
}
```

`color` 为线性空间 RGBA，每个分量位于 0..1。v1 不包含纹理、Shader 参数或材质图。

Material v2 将 `color` 解释为 base-color 因子，并增加必需的 Texture 描述 URI：

```json
{
  "format": "gneiss.material",
  "version": 2,
  "color": [1.0, 1.0, 1.0, 1.0],
  "base_color_texture": "asset://textures/white.texture.json"
}
```

Material 租约持有其 Texture 租约；Material 释放前依赖的 Texture RID 始终有效。

Material v3 增加标准 PBR 的金属度与感知粗糙度，并允许没有基础颜色纹理：

```json
{
  "format": "gneiss.material",
  "version": 3,
  "color": [1.0, 1.0, 1.0, 1.0],
  "base_color_texture": null,
  "metallic": 0.0,
  "roughness": 1.0
}
```

`metallic` 与 `roughness` 均位于 0..1。它们属于 Gneiss 作者资产语义，运行时再投影为
Granit 标准 PBR Material 参数；作者资产不引用 `.grmat` 或后端 Shader。后端投影使用 Granit
公开的标准 PBR Schema、Material 和 Shader Asset 元数据契约，Gneiss 不解析或复制其私有布局。

Material v4 在 v3 基础上增加可选的 `metallic_roughness_texture`、`normal_texture`、
`occlusion_texture`、`emissive_texture` URI（省略或 null 使用中性默认纹理），以及
`normal_scale`（默认 1，有限非负值）、`occlusion_strength`（默认 1，0..1）、
`emissive`（默认 `[0,0,0]`，线性 RGB 各分量 0..1）。MR 的 G/B 通道分别保存粗糙度/金属度，AO 使用 R。
基础颜色和自发光纹理应声明 sRGB，其余槽声明 linear。材质租约和帧快照保留所有纹理依赖，
异步材质重载将全部依赖作为一个事务准备；任一项失败不会发布部分材质。

此格式和运行链路不表示 glTF 导入已保留全部材质，也不表示支持透明、镂空或多 UV。
法线贴图还需要有效的网格切线；当前 0.43 开发进度见 [版本计划](../plans/VER-043-0.43.0-pbr-pipeline.md)。

## Texture

建议扩展名为 `.texture.json`：

```json
{
  "format": "gneiss.texture",
  "version": 1,
  "source": "asset://textures/white.png",
  "color_space": "srgb"
}
```

作者资产中的 `source` 可以指向 PNG；`color_space` 必须为 `srgb` 或 `linear`，由描述文件明确指定，
不从 PNG 元数据推断。资产构建会生成直到 1×1 的完整 Mip 链，再确定性转换为同名
`.gneiss-texture` 并重写运行资产中的 URI。相同图像不能由多个 Texture 描述同时声明为不同颜色空间。

0.34.0 的运行容器只接受二维、单层、单面、无超级压缩的 `R8G8B8A8_UNORM` 或
`R8G8B8A8_SRGB` KTX2。Runtime 校验标识、DFD 传递函数、完整 Mip 数量、Level Index 范围和每级
字节数，再将全部 Mip 交给 Granit。图片宽高上限为 16384，解码后资源数据上限为 256 MiB。

`.gneiss-texture` 包含 Gneiss 外层 Header、Granit Texture Asset Manifest、BC7 优选负载和 RGBA8
回退负载。两种变体使用相同完整 Mip 链；颜色空间决定对应的 UNORM 或 SRGB GPU 格式。编辑器直接
运行尚未 Cook 的作者工程时保留 PNG 兼容路径。PNG 解码、Mip 生成和 BC7 编码只存在于工具路径。

运行纹理封装的 Runtime 加载要求启用 `GNEISS_ENABLE_GRANIT_PLATFORM`；关闭时返回
`GNEISS_ERROR_UNSUPPORTED` 并定位到 `/source`，不创建纹理 RID。PNG 与 KTX2 的 CPU 加载仍可用。

`gneiss_assetc inspect <file.gneiss-texture>` 校验外层封装、Manifest、二维完整 Mip 链、颜色空间、
RGBA8 回退与所有变体的负载 SHA-256，输出尺寸、变体格式和各 Mip 的字节范围、行跨度。
`validate <file.gneiss-texture>` 执行相同校验，仅输出通过提示。失败时向标准错误输出诊断并返回 1；
离线检查不判断当前 GPU 是否支持某种格式。

启用 Application 日志回调时，运行纹理首次建立 GPU 镜像会产生 `render.texture` 分类日志，来源为
`granit.render.texture`。消息包含 RID、阶段、变体下标、格式、Mip 数与所选负载字节数；缓存命中不
重复记录。`stage=ready` 表示批次提交和 Texture View 创建成功，不表示 GPU 已完成执行或最终画面
已验收。失败阶段为 `inspect`、`select`、`create`、`batch`、`write`、`submit` 或 `view`，结果码随日志
返回。日志经既有有界队列异步投递，极端拥塞时遵循日志队列的丢弃策略。

## 加载与生命周期

Loader 依次执行 VFS 读取、严格 JSON 校验、创建 Render RID 和缓存租约。相同 URI 与类型复用 RID；
相同 URI 不能解释为另一种资源类型。格式失败不永久缓存，修复内容后可重试。租约只借出 RID，不转移
销毁权；最后一个租约释放并清理缓存后，Loader 自动销毁 RID。
