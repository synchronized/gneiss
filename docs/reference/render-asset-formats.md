<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Render 资产格式

Runtime Mesh 优先使用二进制；旧 Mesh、Material 与 Texture 描述使用严格 UTF-8 JSON。所有格式仅由
内部 Loader 使用，资源 URI 规则见[资产 URI、目录挂载与缓存](assets.md)。

## Mesh Binary v3

建议扩展名为 `.gneiss-mesh`。只读写 v3；v1、v2 和未知版本返回 `unsupported_version`，
旧二进制资产必须重新导入。多字节整数与 IEEE 754 Float32 固定为小端序，所有 Offset 相对文件起点。

| Offset | 大小 | 字段 | 值或含义 |
| ---: | ---: | --- | --- |
| 0 | 4 | magic | ASCII `GNMS` |
| 4 | 2 | version | `3` |
| 6 | 2 | header_size | `80` |
| 8 | 4 | flags | 0..3；位 0 表示 UV1，位 1 表示颜色 |
| 12 | 4 | vertex_count | 唯一顶点数 |
| 16 | 4 | index_count | UInt32 索引数，必须是 3 的倍数 |
| 20 | 2 | vertex_stride | `72` |
| 22 | 2 | index_size | `4` |
| 24 | 8 | vertex_offset | `80` |
| 32 | 8 | index_offset | 16 字节对齐 |
| 40 | 8 | file_size | 必须等于实际文件大小 |
| 48 | 24 | bounds | 局部 AABB min Float3、max Float3 |
| 72 | 8 | reserved | `0` |

顶点固定依次保存 Position Float3、UV Float2、Normal Float3、Tangent Float4、UV1 Float2、
线性 RGBA Color Float4，共 72 字节；随后填充到 16 字节边界，再保存 UInt32 索引。
UV1 缺失填零、颜色缺失填白，但对应 flags 为零，解码后仍返回空的可选属性数组。
切线必须逐顶点提供，XYZ 为与法线正交的单位向量（误差 `1e-4`），W 为 `+1` 或 `-1`。
编码器不生成切线；缺失或无效直接报错。glTF 导入器保留作者手性并正交化切线，
缺失或可修复的共线切线使用 MikkTSpace 生成；无法生成有效切线时导入失败。

Decoder 验证版本、数量乘法、步长、Offset、文件边界、AABB、索引范围和属性值。
边界必须与顶点计算结果一致；UV 有限，颜色分量位于 0..1。Runtime 向 Render Service 提交
唯一顶点与索引，由后端通过 Index Buffer 绘制。

Mesh 构建处理器版本已提升至 3，旧缓存键失效；应从作者源重新导入并 Cook。
这不改变下文 JSON Mesh 的独立版本规则。

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
采样寻址由 Material 决定，旧版本默认重复寻址。

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

仅读写 `gneiss.material` v5，建议扩展名为 `.material.json`。v1–v4 和未知版本拒绝加载。

```json
{
  "format": "gneiss.material",
  "version": 5,
  "color": [1.0, 1.0, 1.0, 1.0],
  "base_color_texture": null,
  "metallic": 0.0,
  "roughness": 1.0
}
```

`color` 是线性 RGBA，各分量位于 0..1；`metallic` 与 `roughness` 必需且位于 0..1。
`base_color_texture` 必需，为非空 URI 或 null。其余四槽 `metallic_roughness_texture`、
`normal_texture`、`occlusion_texture`、`emissive_texture` 可以省略或为 null。
可选参数包括 `normal_scale`（默认 1，有限值）、`occlusion_strength`（默认 1，0..1）、
`emissive`（默认 `[0,0,0]`，线性 RGB 各分量 0..1）、`alpha_mode`（默认 `OPAQUE`，
另支持 `MASK`、`BLEND`）、`double_sided`（默认 false）、`alpha_cutoff`（默认 0.5，有限非负）。
MR 的 G/B 通道分别保存粗糙度/金属度，AO 使用 R；基础颜色和自发光纹理使用 sRGB，其余使用 linear。
作者材质只保存参数和 URI，不保存 RID 或后端对象。运行期材质及帧快照保留纹理依赖。

`sampling` 省略时五槽均使用 UV0、linear、trilinear、repeat；提供时必须为五项数组，顺序为
base color、MR、normal、AO、emissive，每项要求以下六个整数：

| 字段 | 值 |
| --- | --- |
| `uv_set` | 0 或 1；引用 UV1 时网格必须提供 UV1 |
| `mag_filter` / `min_filter` | 0 nearest、1 linear |
| `mip_filter` | 0 禁用 Mip、1 nearest、2 linear |
| `address_u` / `address_v` | 0 repeat、1 clamp、2 mirror |

glTF 导入输出 v5，保留五类纹理、逐槽 UV/采样、Alpha 模式、双面及 COLOR_0。
UV2 及以上显式拒绝；法线缺切线时按其所用 UV 集生成。透明由 Granit 在每 View 的不透明阶段后
进行对象级排序；不保证相交三角形的精确透明顺序，不包含折射/OIT。
GPU 使用统一包含 UV1/Color 的布局与对应标准变体；缺失顶点色补白色，基础颜色与 Alpha 各乘一次。
负缩放的绕序与切线空间修正由 Granit 公共管线处理。

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

Texture v2 在 v1 基础上要求 `usage: "normal"` 和 `color_space: "linear"`，用于声明法线 Mip。
Cook 对 sRGB 颜色先解码至线性光域再过滤；线性数据直接过滤；法线按向量过滤并归一化，
相消时回退为 +Z。Alpha 始终线性过滤，奇数尺寸使用面积权重覆盖边缘像素。
glTF 同图跨颜色/数据/法线用途时生成独立派生路径，MR 与 AO 共用 linear 变体，
base color 与 emissive 共用 sRGB 变体。纹理处理器版本与用途均进入缓存身份。
材质按逐槽 Mip 过滤设置访问 Mip 链；尚未 Cook 的 PNG 兼容路径仍只有基础级。

0.34.0 的运行容器只接受二维、单层、单面、无超级压缩的 `R8G8B8A8_UNORM` 或
`R8G8B8A8_SRGB` KTX2。Runtime 校验标识、DFD 传递函数、完整 Mip 数量、Level Index 范围和每级
字节数，再将全部 Mip 交给 Granit。图片宽高上限为 16384，解码后资源数据上限为 256 MiB。

`.gneiss-texture` 包含 Gneiss 外层 Header、Granit Texture Asset Manifest、BC7 优选负载和 RGBA8
回退负载。两种变体使用相同完整 Mip 链；颜色空间决定对应的 UNORM 或 SRGB GPU 格式。编辑器直接
运行尚未 Cook 的作者工程时保留 PNG 兼容路径。PNG 兼容加载包含 CPU 解码；Mip 生成和 BC7 编码只存在于工具路径。

运行纹理封装的 CPU 解码使用固定依赖 Granit 的 Manifest 接口，不要求创建窗口或初始化 GPU。
PNG 与 KTX2 的 CPU 加载同样不依赖窗口。

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
