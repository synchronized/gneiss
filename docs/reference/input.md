<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 输入接口

## 当前能力

`gneiss/engine/input.h` 提供不依赖平台后端的 C11 输入 ABI，`gneiss/engine/input.hpp` 提供轻量 C++20 包装。
Application 使用 Granit 平台时，会在每次业务更新回调前完成窗口事件采集，并形成当前帧键盘和
指针快照。核心无窗口模式下快照保持为空，事件查询返回 `GNEISS_ERROR_NOT_READY`。

原始事件通过 `gneiss_application_poll_input` 顺序读取，包含逻辑窗口 ID、单调时钟时间戳以及键盘、
UTF-8 文本或指针负载。物理键值采用 USB HID Keyboard/Keypad usage；指针坐标是窗口内容区逻辑
坐标。文本负载最多保存 48 字节，且由后端保证不截断 UTF-8 码点。

## 帧与生命周期

- 输入只能在 Application 创建线程查询。
- 当前帧事件在下一次平台轮询开始时清空；业务应在当帧 `update` 回调内消费。
- 键盘和指针查询返回值副本，不借用后端内存。
- 焦点丢失会清空按键、指针按钮和窗口内状态，避免保持状态卡住。
- 首版队列固定容纳 256 条事件；溢出时清空保持状态并令 Application 本帧返回
  `GNEISS_ERROR_INVALID_STATE`，不会静默丢弃释放事件。
- Application 销毁后，使用旧句柄查询返回 `GNEISS_ERROR_INVALID_HANDLE`。

Granit 的类型、句柄和枚举只存在于 `src/engine/platform/granit/`，不属于 Gneiss 公共 ABI。版本化动作
映射在原始快照之上提供 `pressed`、`held`、`released` 和标量值，格式与句柄规则见
[输入动作映射格式 v1](input-map-format.md)。

## C++ 入口

`application` 提供 `poll_input`、`get_keyboard_state`、`get_pointer_state`、`load_action_map`、
`find_action` 和 `get_action_state` 成员；不需要先取出裸 Application 句柄。
事件和状态是独立的 C++ 值类型，使用默认构造，不包含 `struct_size` 和保留字段。
键盘的 `physical_key`、`logical_key`、`key_action` 为强类型枚举；`input_modifier` 和
`pointer_button` 支持同类标志的 `|`、`&`、复合赋值及 `has_flags`。
`keyboard_state::is_pressed` 接收物理键，超出 256 位快照范围返回 false。

`input_event::data` 为 `input_event_data` variant，可用 `std::get_if` 或 `std::visit` 访问
`key_event`、`text_event`、三类指针数据与进入/离开标记；默认分支是 `std::monostate`。
事件种类由活动分支表达，不再保存另一份可能不一致的整数标签。文本自有存储，复制事件不会借用
原始 C 事件；`text_event::text()` 返回当前对象的借用视图，不保证零终止，移动/销毁后需重新获取。

查询失败保留输出。未知事件或未知按键动作返回 `unsupported`，非法文本长度返回
`invalid_argument`；此时原始事件已出队，不可重试同一事件。未命名物理/逻辑键和标志保留原始
32 位值，避免静默映射为已知值。线程与动作失效规则沿用 C API。

```cpp
gneiss::keyboard_state keyboard{};
if (app.get_keyboard_state(keyboard).ok() && keyboard.is_pressed(gneiss::physical_key::w)) {
  // 在当前更新回调中处理移动。
}
gneiss::input_event event{};
while (app.poll_input(event).ok()) {
  if (const auto* text = std::get_if<gneiss::text_event>(&event.data)) {
    // text->text() 只借用本次 event 的文本；需要长期保存时复制。
    consume_text(text->text());
  }
}
```

上例假设 `app` 是已创建的 Application，`consume_text` 是调用方同步消费文本的函数。

`action_id` 是非拥有的强类型标识，不能隐式当作实体或其他整数句柄。动作映射成功重载后旧 ID
失效，加载失败保留原映射；ID 不能跨 Application 使用。`find_action` 的强类型重载失败时保留
原输出，非零 ID 不保证映射仍存活。`action` 整数别名及裸动作重载已移除；需要显式 Application 句柄互操作时可使用自由函数，动作仍使用 `action_id`。
