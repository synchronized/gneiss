<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Render 源资产格式 v1

描述文件均为严格 UTF-8 JSON，仅由内部 Loader 使用。资源 URI 规则见
[资产 URI、目录挂载与缓存](assets.md)。

## Mesh

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

Mesh v2 保留 `vertices`，并增加数量必须与顶点一致的 `uvs`；每项是位于 0..1 的 `(u, v)`：

```json
{
  "format": "gneiss.mesh",
  "version": 2,
  "topology": "triangle_list",
  "vertices": [[-0.5, -0.5, 0], [0.5, -0.5, 0], [0, 0.5, 0]],
  "uvs": [[0, 0], [1, 0], [0.5, 1]]
}
```

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

`source` 是通过 VFS 读取的 PNG URI；首版使用 libspng 解码并统一输出紧密排列的 RGBA8。
`color_space` 必须为 `srgb` 或 `linear`，由描述文件明确指定，不从 PNG 元数据推断。图片宽高上限为
16384，解码后像素数据上限为 256 MiB。解码器属于 Loader 私有实现，不进入公共 API 或安装包依赖。

## 加载与生命周期

Loader 依次执行 VFS 读取、严格 JSON 校验、创建 Render RID 和缓存租约。相同 URI 与类型复用 RID；
相同 URI 不能解释为另一种资源类型。格式失败不永久缓存，修复内容后可重试。租约只借出 RID，不转移
销毁权；最后一个租约释放并清理缓存后，Loader 自动销毁 RID。
