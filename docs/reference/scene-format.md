<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 场景文件格式 v5

场景使用严格 UTF-8 JSON，格式标识为 `gneiss.scene`，当前 Schema 版本为 `5`，建议文件扩展名为
`.scene.json`。场景加载入口通过异步文件服务读取该格式，具体生命周期见
[场景加载、实例与卸载](scene-instance.md)。

## 最小结构

```json
{
  "format": "gneiss.scene",
  "version": 5,
  "scene_uuid": "00000000-0000-4000-8000-000000000001",
  "objects": [
    {
      "uuid": "00000000-0000-4000-8000-000000000002",
      "name": "Triangle",
      "parent": null,
      "transform": {
        "translation": [
          0,
          0,
          0
        ],
        "rotation": [
          0,
          0,
          0,
          1
        ],
        "scale": [
          1,
          1,
          1
        ]
      },
      "components": [{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://models/triangle.mesh","material":"asset://materials/default.material"}}]
    },
    {
      "name": "Lamp Instance",
      "parent": "00000000-0000-4000-8000-000000000002",
      "prefab": {
        "uri": "asset://prefabs/lamp.prefab.json",
        "overrides": [
          {
            "source_node_uuid": "10000000-0000-4000-8000-000000000002",
            "value": {
              "kind": "vec3",
              "value": [
                0,
                1,
                0
              ]
            },
            "type_guid": "69644f20b2d24e488c7491f4f952ec2d",
            "property_id": 1
          }
        ]
      },
      "transform": {
        "translation": [
          2,
          0,
          0
        ],
        "rotation": [
          0,
          0,
          0,
          1
        ],
        "scale": [
          1,
          1,
          1
        ]
      },
      "uuid": "00000000-0000-4000-8000-000000000010"
    }
  ]
}
```

## 字段规则

- UUID 必须使用非零的 36 字符小写规范形式；对象 UUID 在场景内唯一。
  内存中的场景、对象、Prefab 来源及覆盖键使用 `engine::guid`；`object_description::parent_uuid`
  使用 `std::optional<guid>`，`nullopt` 表示根，不能用 nil GUID 代替根。JSON 仍使用字符串和 `null`。
- `name` 是可选 UTF-8 显示名称；缺失或为空时，工具可回退显示 UUID。
- `parent` 必须是现有对象 UUID 或 `null`；层级不能形成循环。
- 向量与四元数只接受有限、可表示为 float 的 JSON 数字。
- rotation 顺序为 `(x, y, z, w)` 且必须归一化；scale 的每个分量不能为零。
- `components` 是数组，可为空。每项包含 `$type_guid`（描述类型的规范 GUID）和 `$content`（JSON 对象）。
  根据默认 Meta 注册表查找对应 codec；同一对象同一 GUID 最多一项，未知类型和重复类型报错。
- 内置 Camera 描述 GUID 为 `11a0217a-2eab-4f61-8011-4a63a26e0101`，MeshRenderer 描述 GUID 为
  `11a0217a-2eab-4f61-8011-4a63a26e0102`。它们与运行时组件 GUID 分离，进程内 type_id 不写入文件。
- 当前运行时只安装 Camera、MeshRenderer。其他已注册 codec 的描述可读写，实例化返回 unsupported。
  组件数组顺序不表示身份；Prefab 属性覆盖仍按来源节点、运行时组件类型和属性 ID 定位。
- Camera 要求 `vertical_field_of_view_radians` 位于 `(0, π)`、`near_plane > 0`、
  `far_plane > near_plane` 和布尔 `is_primary`；一个场景最多有一个主相机。
- Mesh Renderer 的 `mesh` 与 `material` 必须是规范 `asset://` URI。
- `objects` 是唯一对象列表。对象可通过 `prefab: {uri, overrides}` 引用模板；`uri` 必须是规范资产
  URI，`overrides` 必须是数组。此时组件来自模板，拒绝同时提供非空 `components`。
- `parent` 可引用任意显式对象，包括 Prefab 根；不能引用来源内部节点 UUID。普通自有子对象正常保存，
  模板生成的子节点不重复写入列表。模板文件自身暂不允许嵌套 Prefab 引用。
- 每项覆盖以来源节点 UUID、32 字符小写十六进制 `type_guid` 和非零 uint32 `property_id` 定位字段。`value`
  使用显式 `kind` 与值；当前类别为 `bool`、`int64`、`uint64`、`float32`、`float64`、`string`、
  `type_id`、`vec3` 和 `quaternion`。同一实例内的完整覆盖键不能重复。
- Prefab 实例身份就是引用对象的 UUID，覆盖键不重复携带实例身份。Prefab 展开后的源节点、Entity
  ID、Scene Node ID 和 RID 均不写入场景文件。
- 普通对象需要 `components`，模板引用对象需要 `prefab`；`name` 可选。当前受支持版本中的未知字段不会参与运行时
  实例化；对象和组件外壳的扩展字段保留在作者 JSON 中，重新输出时按 UUID/GUID 对齐。
  `$content` 由对应 codec 完整输出，不与旧内容合并，避免已删除或省略的字段重新出现；
  需要保留内容扩展字段的类型须由自身 codec 读写这些字段。

保存时直接校验内存中的对象层级、变换、Prefab 引用和覆盖；内置组件编码器校验自身参数。
自定义组件编码器负责其业务合法性，保存不会再次调用组件解码器验证输出。
组件编码结果仍必须是合法 JSON 对象，才能拼装为 `$content`。Scene / Prefab 的对象编解码
共用实现，Scene 允许多根森林，Prefab 按显式根要求单树。

解析成功只产生中间描述，不修改 World。语法错误诊断使用 UTF-8 文档的零起始字节偏移，字段错误
使用类似 `/objects/0/transform` 的 JSON 路径。未来版本返回 `GNEISS_ERROR_UNSUPPORTED`。

## Prefab 根语义

Prefab v1 顶层必须包含 `prefab_uuid`、`root_uuid` 和 `objects`。`prefab_uuid` 是资产身份，
`root_uuid` 是对象树入口，两者独立；内存中均为 `guid`，文件中为非零规范小写 UUID。
`root_uuid` 必须指向唯一 `parent: null` 的对象，其他对象沿父链必须能到达该根。
拒绝缺失根、多个根、孤立对象、缺失父节点、重复 UUID 和环。对象数组不要求父对象在前，
根身份不由数组顺序决定。未发布的旧文件直接补充该字段，不提供缺失时的推断回退。

来源根直接对应实例对象，不再自动创建空容器节点。根的名称、父节点和局部 TRS 使用实例字段，
不与模板根变换叠乘。根组件仍来自模板；来源子节点保留模板局部变换与合法覆盖。
根 Transform 覆盖被拒绝，整体变换直接修改实例字段。来源重载保留根节点身份和实例值；
更换来源根 UUID 需要显式重新实例化，当前原地重载返回 `unsupported`。
解包直接生成来源对象数量的普通节点，来源根采用实例名称、父节点和变换，不生成额外父节点。

## 版本兼容

项目尚未对场景 Schema 作出外部兼容承诺。当前只接受 v5：旧版本返回
`GNEISS_ERROR_INVALID_ARGUMENT`，未来版本返回 `GNEISS_ERROR_UNSUPPORTED`。仓库内作者场景、示例
和测试随格式直接升级，不维护尚无真实消费者的迁移链。解析或校验失败时场景实例化尚未开始，现有
World 保持不变。旧顶层 `prefab_instances` 也被明确拒绝，不作为扩展字段忽略。

## 内存描述与初始化

`object_description::transform` 直接使用 `math::transform`，表示相对父对象的局部变换，默认单位变换。
文件中的 `transform` 及其 translation、rotation、scale 仍全部必填，默认值不代替格式校验。

`object_description::components` 使用 `component_descriptions`，内部保存不可变的类型化描述条目。
`get<T>()` 借用只读指针，错误类型返回 nullptr；`set(T)` 替换对应类型，`erase<T>()` 删除，
`update<T>(callback)` 在副本上修改后替换。借用指针不得跨对应条目替换或列表销毁。
复制列表共享不可变内容；修改副本不改变模板、其他实例或编辑器撤销快照。

解析/编码前由宿主完成 `initialize_meta`。文件解析只生成 CPU 描述，不创建 World 或 GPU 对象。
场景功能层解析资产依赖并安装运行时组件；Meta 不依赖 World。
未发布格式直接迁移为数组，不再接受旧的 components 对象形式。
