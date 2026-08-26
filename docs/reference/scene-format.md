<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景文件格式 v1

场景使用严格 UTF-8 JSON，格式标识为 `gneiss.scene`，当前 Schema 版本为 `1`，建议文件扩展名为
`.scene.json`。当前解析接口仍是内部能力，将由后续场景加载 API 使用。

## 最小结构

```json
{
  "format": "gneiss.scene",
  "version": 1,
  "scene_uuid": "00000000-0000-4000-8000-000000000001",
  "objects": [
    {
      "uuid": "00000000-0000-4000-8000-000000000002",
      "parent": null,
      "transform": {
        "translation": [0, 0, 0],
        "rotation": [0, 0, 0, 1],
        "scale": [1, 1, 1]
      },
      "components": {
        "mesh_renderer": {
          "mesh": "asset://models/triangle.mesh",
          "material": "asset://materials/default.material"
        }
      }
    }
  ]
}
```

## 字段规则

- UUID 必须使用 36 字符的小写规范形式；对象 UUID 在场景内唯一。
- `parent` 必须是现有对象 UUID 或 `null`；层级不能形成循环。
- 向量与四元数只接受有限、可表示为 float 的 JSON 数字。
- rotation 顺序为 `(x, y, z, w)` 且必须归一化；scale 的每个分量不能为零。
- `components` 可以为空对象；只允许 `camera` 与 `mesh_renderer`。
- Camera 要求 `vertical_field_of_view_radians` 位于 `(0, π)`、`near_plane > 0`、
  `far_plane > near_plane` 和布尔 `primary`；一个场景最多有一个主相机。
- Mesh Renderer 的 `mesh` 与 `material` 必须是规范 `asset://` URI。
- v1 的所有已列字段均为必需字段；同版本未知字段会被拒绝。

解析成功只产生中间描述，不修改 World。语法错误诊断使用 UTF-8 文档的零起始字节偏移，字段错误
使用类似 `/objects/0/transform` 的 JSON 路径。未来版本返回 `GNEISS_ERROR_UNSUPPORTED`。
