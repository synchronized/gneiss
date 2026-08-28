<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 工程文件格式 v1

Editor 以工程为启动单位。无参数启动时先显示 Project Manager；工程根目录必须包含
固定名称的 `gneiss.project.json`。`--project` 和 Project Manager 都只接收工程根目录，不接受
工程文件路径。首版工程描述如下：

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

最近工程属于用户状态，不写入工程目录。Editor 当前把最近十个有效工程根保存到用户配置目录的
`Gneiss/editor.json`（Linux 使用 `$XDG_CONFIG_HOME/gneiss/editor.json`，未设置时使用
`$HOME/.config/gneiss/editor.json`）。失效工程会在读取时忽略。窗口布局和最近场景等状态尚未实现。

Project Manager 可以创建最小工程：目标目录必须尚不存在，创建结果包含工程描述、`assets/`
和带主 Camera 的 `asset://scenes/main.scene.json`。创建过程先在同级临时目录完整写入，再重命名为
目标目录，失败时不会留下可被误认为完整工程的目标目录。

Project Manager 与正式 Editor 使用两个连续且互不共享运行时状态的 Application。选择工程成功后，
选择窗口及其 UI、渲染和平台资源会先完整销毁，再按工程资产根创建正式 Application。Windows 使用
系统目录选择器；其他平台在原生选择器接入前可直接输入工程路径。
