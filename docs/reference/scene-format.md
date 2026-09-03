<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景文件格式 v4

场景使用严格 UTF-8 JSON，格式标识为 `gneiss.scene`，当前 Schema 版本为 `4`，建议文件扩展名为
`.scene.json`。公共场景实例接口通过 VFS 加载该格式，具体生命周期见
[场景加载、实例与卸载](scene-instance.md)。

## 最小结构

```json
{
  "format": "gneiss.scene",
  "version": 4,
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
  ],
  "prefab_instances": [
    {
      "instance_uuid": "00000000-0000-4000-8000-000000000010",
      "name": "Lamp Instance",
      "parent": "00000000-0000-4000-8000-000000000002",
      "prefab": "asset://prefabs/lamp.prefab.json",
      "transform": {
        "translation": [2, 0, 0],
        "rotation": [0, 0, 0, 1],
        "scale": [1, 1, 1]
      },
      "overrides": [
        {
          "source_node_uuid": "10000000-0000-4000-8000-000000000002",
          "type_id": "69644f20b2d24e488c7491f4f952ec2d",
          "field_id": 1,
          "value": {"kind": "vec3", "value": [0, 1, 0]}
        }
      ]
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
- `prefab_instances` 必须是数组。每项使用唯一 `instance_uuid`、规范 `prefab` URI、实例根
  `transform`，以及普通场景对象 UUID 或 `null` 作为 `parent`；首版拒绝以另一个 Prefab 实例为
  父级。实例名称 `name` 可选；`overrides` 是必需数组，可以为空。
- 每项覆盖以来源节点 UUID、32 位小写十六进制 Type ID 和非零 uint32 Field ID 定位字段。`value`
  使用显式 `kind` 与值；当前类别为 `bool`、`int64`、`uint64`、`float32`、`float64`、`string`、
  `type_id`、`vec3` 和 `quaternion`。同一实例内的完整覆盖键不能重复。
- 对象 UUID 与 Prefab 实例 UUID 共用场景作者身份空间，不能重复。Prefab 展开后的源节点、Entity
  ID、Scene Node ID 和 RID 均不写入场景文件。
- 除 `name` 外，v4 的所有已列字段均为必需字段。当前受支持版本中的未知字段不会参与运行时
  实例化，但会保留在作者 JSON 中，并在 `serialize_scene_description` 重新输出时保持其值和层级。

解析成功只产生中间描述，不修改 World。语法错误诊断使用 UTF-8 文档的零起始字节偏移，字段错误
使用类似 `/objects/0/transform` 的 JSON 路径。未来版本返回 `GNEISS_ERROR_UNSUPPORTED`。

## 版本兼容

项目尚未对场景 Schema 作出外部兼容承诺。当前只接受 v4：旧版本返回
`GNEISS_ERROR_INVALID_ARGUMENT`，未来版本返回 `GNEISS_ERROR_UNSUPPORTED`。仓库内作者场景、示例
和测试随格式直接升级，不维护尚无真实消费者的迁移链。解析或校验失败时场景实例化尚未开始，现有
World 保持不变。
