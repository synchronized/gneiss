<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景文件格式 v2

场景使用严格 UTF-8 JSON，格式标识为 `gneiss.scene`，当前 Schema 版本为 `2`，建议文件扩展名为
`.scene.json`。公共场景实例接口通过 VFS 加载该格式，具体生命周期见
[场景加载、实例与卸载](scene-instance.md)。

## 最小结构

```json
{
  "format": "gneiss.scene",
  "version": 2,
  "scene_uuid": "00000000-0000-4000-8000-000000000001",
  "objects": [
    {
      "uuid": "00000000-0000-4000-8000-000000000002",
      "name": "Triangle",
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
- `name` 是可选 UTF-8 显示名称；缺失或为空时，工具可回退显示 UUID。
- `parent` 必须是现有对象 UUID 或 `null`；层级不能形成循环。
- 向量与四元数只接受有限、可表示为 float 的 JSON 数字。
- rotation 顺序为 `(x, y, z, w)` 且必须归一化；scale 的每个分量不能为零。
- `components` 可以为空对象；只允许 `camera` 与 `mesh_renderer`。
- Camera 要求 `vertical_field_of_view_radians` 位于 `(0, π)`、`near_plane > 0`、
  `far_plane > near_plane` 和布尔 `is_primary`；一个场景最多有一个主相机。
- Mesh Renderer 的 `mesh` 与 `material` 必须是规范 `asset://` URI。
- 除 `name` 外，v2 的所有已列字段均为必需字段。当前受支持版本中的未知字段不会参与运行时
  实例化，但会保留在作者 JSON 中，并在 `serialize_scene_description` 重新输出时保持其值和层级。

解析成功只产生中间描述，不修改 World。语法错误诊断使用 UTF-8 文档的零起始字节偏移，字段错误
使用类似 `/objects/0/transform` 的 JSON 路径。未来版本返回 `GNEISS_ERROR_UNSUPPORTED`。

## 版本迁移

加载 v1 时会按显式迁移链升级到 v2：Camera 的 `primary` 字段重命名为 `is_primary`，其他已知字段
及未知扩展字段保持不变。迁移后才执行当前 Schema 的完整校验，因此损坏旧数据不会进入运行时。
迁移目标字段已经存在时拒绝文档，避免静默覆盖扩展数据。

版本低于最早支持版本或迁移链有缺口时返回 `GNEISS_ERROR_INVALID_ARGUMENT`，高于当前版本时返回
`GNEISS_ERROR_UNSUPPORTED`。迁移只处理内存中的作者文档，不覆盖来源文件；解析、迁移或校验失败
时，场景实例化尚未开始，现有 World 保持不变。
