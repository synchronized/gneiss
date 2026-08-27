<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 输入动作映射格式 v1

动作映射是 UTF-8 严格 JSON 文档，通过 VFS 逻辑 URI 读取。根对象只能包含 `format`、`version` 和
`actions`：

```json
{
  "format": "gneiss.input-map",
  "version": 1,
  "actions": [
    {
      "name": "move_horizontal",
      "bindings": [
        { "key": 4, "scale": -1.0 },
        { "key": 7, "scale": 1.0 }
      ]
    }
  ]
}
```

`name` 是非空、区分大小写的 UTF-8 字节串，同一文档内不可重复。`bindings` 至少包含一项；`key`
是 1～255 的 USB HID Keyboard/Keypad usage，零值 unknown 不可绑定；`scale` 是 `[-1, 1]` 内的
非零有限数。

未知字段、未知格式或版本、空动作列表、重复名称、空绑定及非法数值均返回
`GNEISS_ERROR_INVALID_ARGUMENT`。解析采用事务语义：失败不会修改调用方已有映射。

使用 `gneiss_application_load_action_map` 从逻辑 URI 同步替换映射，再通过
`gneiss_application_find_action` 缓存动作句柄，并用 `gneiss_application_get_action_state` 查询当帧
状态。成功替换映射后旧句柄失效；失败时旧映射和句柄保持有效。动作句柄不可跨 Application 使用，
也不可序列化。
