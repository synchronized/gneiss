<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# Mesh 与 Material 资产格式 v1

两种资产均为严格 UTF-8 JSON，仅由内部 Loader 使用。资源 URI 规则见
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

## 加载与生命周期

Loader 依次执行 VFS 读取、严格 JSON 校验、创建 Render RID 和缓存租约。相同 URI 与类型复用 RID；
相同 URI 不能解释为另一种资源类型。格式失败不永久缓存，修复内容后可重试。租约只借出 RID，不转移
销毁权；最后一个租约释放并清理缓存后，Loader 自动销毁 RID。
