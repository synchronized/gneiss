<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 工程文件格式 v1

Editor 以工程为启动单位。工程根目录必须包含 `gneiss.project.json`，也可以直接把该文件路径传给
Editor。首版工程描述如下：

```json
{
  "format": "gneiss.project",
  "version": 1,
  "name": "My Game",
  "asset_root": "assets",
  "startup_scene": "asset://scenes/main.scene.json"
}
```

## 字段

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `format` | string | 固定为 `gneiss.project` |
| `version` | unsigned integer | 当前固定为 `1` |
| `name` | string | 非空工程显示名称 |
| `asset_root` | string | 相对工程根目录的资产目录，使用正斜杠 |
| `startup_scene` | string | Editor 启动时打开的规范 `asset://` 场景 URI |

`asset_root` 不接受绝对路径、空段、`.`、`..`、反斜杠、冒号或百分号编码。解析器会解析真实路径，
拒绝指向工程根目录之外的目录；`startup_scene` 也必须存在于该资产根内。工程文件决定 Application
的资产挂载和首次打开场景，不负责保存场景内容。

Editor 的本机布局、最近打开场景等用户状态未来应写入 `.gneiss/editor.json`，不得混入工程文件。
该用户状态文件当前尚未实现。
