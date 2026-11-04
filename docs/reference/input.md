<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Gneiss contributors -->

# 输入接口

## 当前能力

完整输入 C API 与 `gneiss/engine/input.h` 已删除；仓内调用使用原生 Application，
不再提供游戏模块动作查询协议。
内部输入值位于 `src/engine/core/input/input_types.hpp`，服务位于 `src/engine/function/input`。
内部 Application 直接查询原生事件及动作，不再维护 C++ 输入包装或第二份输入值类型。
`poll_input`、`find_action`、`get_action_state` 拒绝非所属线程和已关闭对象，返回 `invalid_state`；
键盘/指针快照引用只在所属线程、Application 有效期间使用。不再安装 C++ 包装头。
Application 使用 Granit 平台时，会在每次业务更新回调前完成窗口事件采集，并形成当前帧键盘和
指针快照。核心无窗口模式下快照保持为空，事件查询返回 `error::not_ready`。

原始事件通过 `application::poll_input` 顺序读取，包含逻辑窗口 ID、单调时钟时间戳以及键盘、
UTF-8 文本或指针负载。物理键值采用 USB HID Keyboard/Keypad usage；指针坐标是窗口内容区逻辑
坐标。文本负载最多保存 48 字节，且由后端保证不截断 UTF-8 码点。

## 帧与生命周期

- 输入只能在 Application 创建线程查询。
- 当前帧事件在下一次平台轮询开始时清空；业务应在当帧 `update` 回调内消费。
- 键盘和指针查询返回引擎快照的借用引用，不暴露后端内存；不要跨帧保存引用。
- 焦点丢失会清空按键、指针按钮和窗口内状态，避免保持状态卡住。
- 首版队列固定容纳 256 条事件；溢出时清空保持状态并令 Application 本帧返回
  `error::invalid_state`，不会静默丢弃释放事件。
- 关闭后事件和动作查询返回 `invalid_state`；对象销毁后不得再访问借用指针。

Granit 的类型、句柄和枚举只存在于 `src/engine/platform/window_service.cpp`，不属于 Gneiss 公共 ABI。版本化动作
映射在原始快照之上提供 `pressed`、`held`、`released` 和标量值，格式与句柄规则见
[输入动作映射格式 v1](input-map-format.md)。

## C++ 入口

`gneiss::runtime::application` 提供 `poll_input`、`keyboard_state`、`pointer_state`、`begin_action_map`、
`action_map_status`、`cancel_action_map`、
`find_action` 和 `get_action_state` 成员；不需要先取出裸 Application 句柄。
事件和状态使用 `gneiss::engine` 中的原生值类型，不包含 `struct_size` 和 ABI 保留字段。
`input_event::type` 指定 `data` 联合体的活动成员；只按对应事件类型访问该成员。
文本保存在事件自身的固定数组中，`length` 指定有效字节数，不保证零终止。
物理/逻辑键码、修饰键和按钮保留整数值及位集合语义。

```cpp
gneiss::engine::input_event event{};
while (host.poll_input(event).ok()) {
  if (event.type == gneiss::engine::event_type::text) {
    consume_text(std::string_view{event.data.text.utf8, event.data.text.length});
  }
}
```

上例假设 `host` 是所属线程中的有效原生 Application，`consume_text` 同步消费文本。
需要长期保存时复制文本。队列为空时 `poll_input` 清空输出并返回 `not_ready`。
平台事件由输入服务入队时检查类型、按键动作和文本长度；没有 C 事件到另一套 C++ 事件的转换层。

`action_id` 是非拥有的强类型标识，其 `value` 为零时无效。动作映射成功重载后旧 ID
失效，加载失败保留原映射；ID 不能跨 Application 使用。`find_action` 和 `get_action_state`
失败时保留输出，非零 ID 不保证映射仍存活。原生入口直接调用 Application 成员函数，
不再提供接收 C Application 句柄的自由函数包装。

## 映射资源加载

`action_map` 直接作为共享的只读 CPU 描述，使用 `asset_ref<const action_map>` 和
`asset_request<action_map>`；不再包裹 action_map_asset。内置 Registry 登记 input-map v1
编解码能力，普通加载采用 asset_manager 的显式 decoder 优先、反射兜底规则。
`encode_action_map` 和反射 `serialize` 可生成 v1 文件；普通字段直接绑定原生描述，
编码与解码均执行业务校验并限制为 1 MiB。入口要求默认注册表已初始化。

通用 load/load_sync 默认复用缓存；Application 的显式加载与异步重载仍重新读取。
加载和解析不修改输入服务，成功后由 Application 在所属线程复制描述并调用 replace_action_map。
读取、解码或采用失败保留旧映射；动作 ID 的失效规则不变。大小上限为 1 MiB，
codec 诊断提供 JSON 字节偏移或字段路径。格式约束仍以[输入映射格式](input-map-format.md)为准。

输入映射只提供异步加载：先附加任务执行器，再调用 begin_action_map；成功仅表示请求被接受。
宿主事件循环在 update 前应用结果，通过 action_map_status 查询终态；不存在同步 load_action_map。
